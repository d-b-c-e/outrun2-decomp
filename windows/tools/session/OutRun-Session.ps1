# OutRun 2006 (decomp): the Play with Recording companion that Wheelkit runs (sana 34164/34207; docs/PLAY-WITH-RECORDING.md
# "The companion"). It mirrors DRIVE's full-session bridge: one owned session per request, never input.
#
#   OutRun-Session.ps1 -Record   -GameDir <install> -Recording <package-root> -Result <run-root> -RequestId <32 hex>
#   OutRun-Session.ps1 -Playback -GameDir <install> -Recording <package-root> -Result <run-root> -RequestId <32 hex>
#                                -RecordingSealSha256 <64 hex> -StartupSealSha256 <64 hex>
#   OutRun-Session.ps1 -Stop     -GameDir <install> -Result <run-root> -RequestId <32 hex>
#
# -Record: preflight (the install record owns this exe, this companion, the finalizer and record-enabled capabilities;
#   [Input] Backend = Shim is the owner's own choice in the INI; no inject.on or other request; the game not running),
#   the startup snapshot (outrun.startup-state@1, <package>\startup: the owner's saves, options.ini and OutRunDesktop.ini
#   as they are, only read; native startup_state.hpp), the native one-shot request, the launch. The process identity
#   (pid + creation FILETIME) is pinned from the launched process itself, and status.json counts only when it names that
#   process. At complete, the managed finalizer seals the capture while the game runs; the descriptor recording.json
#   (outrun.recording-package@2) goes beside the capture once the receipt checks out, with playback true only when the
#   startup snapshot was sealed and the finalizer admits the capture for playback (--read-playback-recording). Then it
#   waits for the owner to close the game (no timeout), reads the recording back and writes result.json. Saved and a
#   clean close are reported apart: an abnormal exit is never a pass, and never removes a saved recording.
# -Playback (Wheelkit docs/OUTRUN-PLAYBACK-CONTRACT.md; sana 37135/37176/37516/37642): the package named by both seals and
#   this exe; the owner's files fingerprinted (never written: the game plays in a private home seeded from the snapshot);
#   the managed --export-playback tape; the native playback request; the launch, muted (DBCE_FFB_MUTE=1); at complete
#   the replay capture (<run-root>\replay\capture) sealed and --compare-recordings; then the exact child closed normally
#   (WM_CLOSE to its windows, never a kill); the owner's files checked unchanged; result.json
#   (outrun.full-session-result@2). Bounded by the tape's length; a timeout is a failure, never a shorter replay.
# -Stop: recording: the request-bound stop (<result>\stop.request with the id), only for the live process this companion
#   launched. Playback: the request-bound cancel (<result>\cancel.request): the -Playback companion closes the child
#   normally and reports a failure (a cancel never passes).
# Exit codes: 0 pass (Record, Playback) / stop or cancel requested (Stop); 1 refused or failed; 2 Record: saved but not a
#   clean close; Playback: complete, the game diverged from the recording (valid evidence, not a pass).
[CmdletBinding()]
param(
    [switch]$Record, [switch]$Stop, [switch]$Playback,
    [Parameter(Mandatory)][string]$GameDir,
    [string]$Recording,
    [Parameter(Mandatory)][string]$Result,
    [Parameter(Mandatory)][string]$RequestId,
    [string]$RecordingSealSha256, [string]$StartupSealSha256,   # Playback: the package Wheelkit prepared
    [ValidateSet('human', 'automated', 'unspecified')][string]$InputOrigin = 'unspecified',   # Wheelkit: human; runners: automated
    [string]$Exe = 'OutRunDesktop.exe',          # test seam: the device-free fixture (tests/session_companion)
    [int]$AdmissionSeconds = 60,
    [int]$PollMilliseconds = 250,
    [int]$CloseSeconds = 90,                     # Playback: the child's normal exit after WM_CLOSE (it saves its capture)
    [ValidateSet('', 'launch')][string]$TestFault = ''   # test seam (tests/session_companion): 'launch' fails the launch after the request
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$Schema = @{ companion = 'outrun.companion-session@1'; result = 'outrun.full-session-result@1'; package = 'outrun.recording-package@2'
             status = 'outrun.session-status@1'; receipt = 'outrun.recording-verified@1'; request = 'outrun.session-request@1'
             startup = 'outrun.startup-state@1'; playbackResult = 'outrun.full-session-result@2'; comparison = 'outrun.consumed-comparison@1' }
# The native side's current limits (ordinary_recording.hpp / shim_input.cpp): reported as limits, never as a run's length.
$Limits = [ordered]@{ rows = 18000; spanSeconds = 299; hz = 60; note = 'current limits of the native recorder, not a demonstrated duration' }
# The startup inventory's limits (native startup_state.hpp, the fence's seed limits): SaveGame at most 4 folders deep,
# 256 files, 64 MiB; plus options.ini and OutRunDesktop.ini.
$StartupLimits = @{ depth = 4; files = 256; bytes = 64MB }

Add-Type -Namespace OutRunSession -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)] public static extern System.IntPtr OpenProcess(uint access, bool inherit, uint processId);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool GetProcessTimes(System.IntPtr process, out long creation, out long exit, out long kernel, out long user);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr handle);
[DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(System.IntPtr process, out uint code);
public delegate bool EnumWindowsProc(System.IntPtr hwnd, System.IntPtr lparam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, System.IntPtr lparam);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hwnd, out uint processId);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr hwnd, uint msg, System.IntPtr wparam, System.IntPtr lparam);
public static int CloseWindowsOf(uint processId) {
    int posted = 0;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == processId && PostMessageW(h, 0x0010, System.IntPtr.Zero, System.IntPtr.Zero)) ++posted; return true; }, System.IntPtr.Zero);
    return posted;
}
'@

function Fail([string]$why) { throw [System.InvalidOperationException]::new($why) }
function Get-Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Read-Shared([string]$path, [int]$max = 1MB) {
    # Readers never block the game's one-step replace of status.json (FILE_SHARE_DELETE), and never read unbounded.
    try {
        $f = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite, Delete')
        try { if ($f.Length -gt $max) { return $null }; $r = [System.IO.StreamReader]::new($f); return $r.ReadToEnd() } finally { $f.Dispose() }
    } catch [System.IO.FileNotFoundException] { return $null } catch [System.IO.DirectoryNotFoundException] { return $null } catch [System.IO.IOException] { return $null }
}
function Read-Json([string]$path) { $t = Read-Shared $path; if ($null -eq $t) { return $null }; try { return $t | ConvertFrom-Json } catch { return $null } }
function Write-Replace([string]$path, [string]$text) {
    $tmp = "$path.tmp-$PID"; [System.IO.File]::WriteAllText($tmp, $text)
    for ($i = 0; $i -lt 20; ++$i) { try { [System.IO.File]::Move($tmp, $path, $true); return } catch [System.IO.IOException] { Start-Sleep -Milliseconds 50 } }
    Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue; Fail "cannot write $path"
}
function Write-New([string]$path, [string]$text) {
    $s = [System.IO.File]::Open($path, 'CreateNew', 'Write', 'None')
    try { $b = [System.Text.UTF8Encoding]::new($false).GetBytes($text); $s.Write($b, 0, $b.Length); $s.Flush($true) } finally { $s.Dispose() }
}
function Get-Creation([System.IntPtr]$handle) {
    $c = 0L; $e = 0L; $k = 0L; $u = 0L
    if (-not [OutRunSession.Native]::GetProcessTimes($handle, [ref]$c, [ref]$e, [ref]$k, [ref]$u)) { return $null }
    return [uint64]$c
}
# The live process with this pid and creation time, else $false (a reused pid is another process).
function Test-Alive([uint64]$processId, [uint64]$creation) {
    $h = [OutRunSession.Native]::OpenProcess(0x1000, $false, [uint32]$processId)   # PROCESS_QUERY_LIMITED_INFORMATION
    if ($h -eq [System.IntPtr]::Zero) { return $false }
    try { $code = 0; [void][OutRunSession.Native]::GetExitCodeProcess($h, [ref]$code); return (Get-Creation $h) -eq $creation -and $code -eq 259 }   # STILL_ACTIVE
    finally { [void][OutRunSession.Native]::CloseHandle($h) }
}
function Test-Id([string]$id) { $id -cmatch '^[0-9a-f]{32}$' }
function Test-Sha([string]$s) { $s -cmatch '^[0-9a-f]{64}$' }
# The capture's span: the session writer's footer (the last line of source.jsonl) holds the last row's elapsedSeconds
# from the first; read from a bounded tail.
function Get-DurationUs([string]$path) {
    $f = [System.IO.File]::Open($path, 'Open', 'Read', 'Read')
    try {
        $n = [int][math]::Min($f.Length, 65536); [void]$f.Seek(-$n, 'End'); $b = [byte[]]::new($n); $got = 0
        while ($got -lt $n) { $r = $f.Read($b, $got, $n - $got); if ($r -le 0) { break }; $got += $r }
    } finally { $f.Dispose() }
    $last = ([System.Text.Encoding]::UTF8.GetString($b, 0, $got).TrimEnd("`r", "`n") -split "`n")[-1]
    $j = $last | ConvertFrom-Json
    if ($j.kind -ne 'footer' -or $null -eq $j.footer.elapsedSeconds) { Fail 'the capture has no footer' }
    return [uint64][math]::Round([double]$j.footer.elapsedSeconds * 1e6)
}
function Test-Within([string]$a, [string]$b) {
    $x = [System.IO.Path]::GetFullPath($a).TrimEnd('\') + '\'; $y = [System.IO.Path]::GetFullPath($b).TrimEnd('\') + '\'
    return $x.StartsWith($y, [System.StringComparison]::OrdinalIgnoreCase)
}
# status.json counts only when it names this request and this process (sana 34207: never status.json alone).
function Get-OwnStatus([string]$resultDir, [string]$id, [uint64]$processId, [uint64]$creation) {
    $s = Read-Json "$resultDir\status.json"
    if ($null -eq $s) { return $null }
    try { if ($s.schema -ne $Schema.status -or $s.requestId -cne $id -or [uint64]$s.processId -ne $processId -or [uint64]$s.processCreationFileTime -ne $creation) { return 'foreign' } }
    catch { return 'foreign' }
    return $s
}
# The game's writable state as the startup inventory sees it (native startup_state.hpp): SaveGame/** (files only, no link,
# at most 4 folders deep, the fence's limits), options.ini and OutRunDesktop.ini. Read only. Returns ordered
# relative path -> @{ path; bytes; sha } (absent ones: $null), and the absent names.
function Get-GameState([string]$gameDir) {
    $members = [ordered]@{}; $absent = @(); $total = 0
    $saves = Join-Path $gameDir 'SaveGame'
    if (Test-Path -LiteralPath $saves -PathType Container) {
        if ((Get-Item -LiteralPath $saves -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) { Fail 'SaveGame is a link; it is never followed' }
        $stack = [System.Collections.Generic.Stack[object]]::new(); $stack.Push(@($saves, 0))
        while ($stack.Count) {
            $dir, $depth = $stack.Pop()
            foreach ($e in Get-ChildItem -LiteralPath $dir -Force) {
                if ($e.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { Fail "SaveGame holds a link: $($e.Name)" }
                if ($e.PSIsContainer) { if ($depth + 1 -gt $StartupLimits.depth) { Fail 'SaveGame is nested deeper than 4 folders' }; $stack.Push(@($e.FullName, ($depth + 1))); continue }
                $rel = 'SaveGame/' + $e.FullName.Substring($saves.Length + 1).Replace('\', '/')
                $total += $e.Length
                $members[$rel] = @{ path = $e.FullName; bytes = [uint64]$e.Length; sha = (Get-Sha $e.FullName) }
                if ($members.Count -gt $StartupLimits.files) { Fail 'SaveGame has more than 256 files' }
                if ($total -gt $StartupLimits.bytes) { Fail 'SaveGame is larger than 64 MiB' }
            }
        }
    } elseif (Test-Path -LiteralPath $saves) { Fail 'SaveGame is not a folder' } else { $absent += 'SaveGame' }
    foreach ($top in 'options.ini', 'OutRunDesktop.ini') {
        $p = Join-Path $gameDir $top
        if (Test-Path -LiteralPath $p -PathType Leaf) {
            if ((Get-Item -LiteralPath $p -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) { Fail "$top is a link; it is never followed" }
            $members[$top] = @{ path = $p; bytes = [uint64](Get-Item -LiteralPath $p).Length; sha = (Get-Sha $p) }
        } else { $absent += $top }
    }
    return @{ members = $members; absent = $absent }
}
# The startup snapshot (outrun.startup-state@1) of the game's state, into a new folder: each member copied, hashed, and its
# source hashed again (a member that changed while it was read fails the snapshot); complete.tsv last. Returns its sha256.
function New-StartupSnapshot([string]$gameDir, [string]$dest, [string]$recordingId, [string]$exeSha) {
    $state = Get-GameState $gameDir
    New-Item -ItemType Directory -Path $dest | Out-Null   # new: refuses an existing folder
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add($Schema.startup); $lines.Add("requestId`t$recordingId"); $lines.Add("exeSha256`t$exeSha")
    foreach ($rel in $state.members.Keys) {
        $m = $state.members[$rel]
        $to = Join-Path $dest ($rel.Replace('/', '\'))
        New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
        [System.IO.File]::Copy($m.path, $to, $false)
        if ((Get-Sha $to) -ne $m.sha -or (Get-Sha $m.path) -ne $m.sha) { Fail "$rel changed while the startup state was read" }
        $lines.Add("file`t$rel`t$($m.bytes)`t$($m.sha)")
    }
    foreach ($a in $state.absent) { $lines.Add("absent`t$a") }
    Write-New "$dest\complete.tsv" (($lines -join "`n") + "`n")
    return Get-Sha "$dest\complete.tsv"
}
# The owner's files as a fingerprint (relative path -> sha, absent ones '-'), for "unchanged" after a playback.
function Get-OwnerFingerprint([string]$gameDir) {
    $state = Get-GameState $gameDir; $f = [ordered]@{}
    foreach ($rel in $state.members.Keys) { $f[$rel] = $state.members[$rel].sha }
    foreach ($a in $state.absent) { $f[$a] = '-' }
    return $f
}
# The install record that owns this exe, this companion and the capabilities (DRIVE's receipt rule), and its finalizer.
# Selected by the whole payload it owns before the ambiguity check, so an earlier record naming the same exe (a
# companion-only update) never disables a session (sana 34661).
function Get-Install([string]$exePath) {
    $exeSha = Get-Sha $exePath
    $companionSha = Get-Sha $PSCommandPath
    $capPath = Join-Path $GameDir 'session-capabilities.json'
    $capSha = if (Test-Path -LiteralPath $capPath) { Get-Sha $capPath } else { '' }
    $owned = @(Get-ChildItem -LiteralPath $GameDir -Filter 'OutRunDesktop.install-*.json' -File | ForEach-Object {
        try { $j = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json } catch { $j = $null }
        if ($j -and $j.schema -eq 'outrun.owner-install@1' -and $j.state -eq 'installed' -and "$($j.installedExeSha256)".ToLowerInvariant() -eq $exeSha -and
            $j.PSObject.Properties['session'] -and "$($j.session.companionSha256)".ToLowerInvariant() -eq $companionSha -and
            "$($j.session.capabilitiesSha256)".ToLowerInvariant() -eq $capSha) { $j } })
    if ($owned.Count -ne 1) { Fail "no single install record owns this $Exe, this companion and its capabilities ($($owned.Count) found)" }
    $session = $owned[0].session
    $finDir = Join-Path $GameDir $session.finalizerDirectory
    if (Test-Within $finDir $GameDir) {} else { Fail 'the finalizer folder is outside the game folder' }
    foreach ($f in $session.finalizer.PSObject.Properties) {
        if ((Get-Sha (Join-Path $finDir $f.Name)) -ne "$($f.Value)".ToLowerInvariant()) { Fail "the finalizer file $($f.Name) is not the installed one" } }
    foreach ($required in 'P21Finalizer.exe', 'P21Finalizer.dll', 'Dbce.Wheel.Recording.dll') {   # as Wheelkit's Capability (sana d7d6084)
        if (-not $session.finalizer.PSObject.Properties[$required]) { Fail "the install record does not own $required" } }
    $cap = Get-Content -LiteralPath $capPath -Raw | ConvertFrom-Json   # its bytes are the selected record's (above)
    if ($cap.schema -ne 'dbce.session-capabilities@1' -or $cap.protocol -ne $Schema.request -or "$($cap.gameBuildSha256)".ToLowerInvariant() -ne $exeSha) {
        Fail 'the capabilities are not this install''s' }
    return @{ exeSha = $exeSha; finalizer = (Join-Path $finDir 'P21Finalizer.exe'); cap = $cap }
}
function Test-Quiet([string]$exePath) {
    New-Item -ItemType Directory -Force -Path $fence | Out-Null
    $stray = @(Get-ChildItem -LiteralPath $fence -Force | Where-Object { $_.Name -like 'session-request.txt*' -or $_.Name -eq 'inject.on' })
    if ($stray.Count) { Fail "the game's request folder already holds $($stray.Name -join ', ')" }
    $running = @(Get-CimInstance Win32_Process -Filter "Name='$Exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ExecutablePath -and $_.ExecutablePath -ieq $exePath })
    if ($running.Count) { Fail "$Exe is already running from $GameDir" }
}
$fence = Join-Path $env:LOCALAPPDATA 'dbce\outrun2006-decomp'
$requestFile = Join-Path $fence 'session-request.txt'

if (@(@($Record, $Stop, $Playback) | Where-Object { $_.IsPresent }).Count -ne 1) { Write-Output 'refused: exactly one of -Record, -Playback and -Stop'; exit 1 }
if (-not (Test-Id $RequestId)) { Write-Output 'refused: -RequestId must be 32 lower-case hex digits'; exit 1 }
$GameDir = [System.IO.Path]::GetFullPath($GameDir); $Result = [System.IO.Path]::GetFullPath($Result)
$exePath = Join-Path $GameDir $Exe

if ($Stop) {
    # Bound to the request and the exact live process this companion launched; it asks the game to save (recording) or
    # the -Playback companion to cancel (playback), nothing else.
    $c = Read-Json "$Result\companion.json"
    if ($null -eq $c -or $c.schema -ne $Schema.companion -or $c.requestId -cne $RequestId) { Write-Output 'refused: no session of this request was started here'; exit 1 }
    $processId = [uint64]$c.processId; $creation = [uint64]$c.processCreationFileTime
    if (-not (Test-Alive $processId $creation)) { Write-Output 'refused: the game this session launched is not running'; exit 1 }
    if ($c.PSObject.Properties['action'] -and $c.action -eq 'playback') {
        Write-Replace "$Result\cancel.request" $RequestId
        Write-Output "cancel requested for playback $RequestId (process $processId); the companion closes the game normally (a cancel never passes)"
        exit 0
    }
    $s = Get-OwnStatus $Result $RequestId $processId $creation
    if ($null -eq $s -or $s -is [string]) { Write-Output 'refused: the game has not acknowledged this recording'; exit 1 }
    if ($s.state -ne 'recording') { Write-Output "not sent: the recording is already $($s.state)"; exit 1 }
    Write-Replace "$Result\stop.request" $RequestId
    Write-Output "stop requested for $RequestId (process $processId); the game saves without quitting"
    exit 0
}

if ($Playback) {
    $state = [ordered]@{ admitted = $false; finalState = $null; stoppedBy = $null; sealed = $false; compared = $false; comparisonCode = $null
                         comparisonSha256 = $null; cancelled = $false; closed = $false; exitCode = $null; ownerStateUnchanged = $false; why = @() }
    $proc = $null; $processId = 0; $creation = 0; $startedUtc = [DateTime]::UtcNow.ToString('o'); $requestText = $null
    $recordingRequestId = $null; $ownerBefore = $null; $ownResult = $false
    function Write-PlaybackResult {
        $complete = $state.compared -and $state.comparisonCode -in 0, 2
        $passed = $state.comparisonCode -eq 0 -and $state.closed -and $state.exitCode -eq 0 -and $state.ownerStateUnchanged -and -not $state.cancelled
        $r = [ordered]@{ schema = $Schema.playbackResult; requestId = $RequestId; action = 'playback'; recordingRequestId = $recordingRequestId
                         recording = $Recording; result = $Result; processId = $processId; processCreationFileTime = $creation
                         startedUtc = $startedUtc; completedUtc = [DateTime]::UtcNow.ToString('o')
                         recordingSealSha256 = $RecordingSealSha256; startupSealSha256 = $StartupSealSha256; comparisonSha256 = $state.comparisonSha256
                         scope = 'full-session'; state = $(if ($complete -and $state.closed -and $state.ownerStateUnchanged -and -not $state.cancelled) { 'complete' } else { 'failed' })
                         admitted = $state.admitted; finalState = $state.finalState; stoppedBy = $state.stoppedBy; sealed = $state.sealed
                         comparisonExitCode = $state.comparisonCode; cancelled = $state.cancelled
                         restored = $state.ownerStateUnchanged; ownerStateUnchanged = $state.ownerStateUnchanged
                         closed = $state.closed; exitCode = $state.exitCode; passed = $passed; why = @($state.why) }
        Write-Replace "$Result\result.json" ($r | ConvertTo-Json -Depth 4)
        return $r
    }
    try {
        # Preflight: refuse before anything is written.
        if (-not $Recording) { Fail '-Playback needs -Recording' }
        if (-not (Test-Sha $RecordingSealSha256) -or -not (Test-Sha $StartupSealSha256)) { Fail '-RecordingSealSha256 and -StartupSealSha256 must be 64 lower-case hex digits' }
        $Recording = [System.IO.Path]::GetFullPath($Recording)
        if (-not (Test-Path -LiteralPath $Recording -PathType Container)) { Fail "no recording at $Recording" }
        if (Test-Path -LiteralPath $Result) { Fail "$Result exists (a new session needs a new folder)" }
        if ((Test-Within $Result $GameDir) -or (Test-Within $Recording $GameDir)) { Fail 'the recording and result folders must be outside the game folder' }
        if ((Test-Within $Recording $Result) -or (Test-Within $Result $Recording)) { Fail 'the recording and result folders must be separate' }
        if (-not (Test-Path -LiteralPath $exePath)) { Fail "no $Exe in $GameDir" }
        $install = Get-Install $exePath; $exeSha = $install.exeSha; $finalizer = $install.finalizer
        $pb = if ($install.cap.PSObject.Properties['playback']) { $install.cap.playback } else { $null }
        if (-not $pb -or -not $pb.enabled -or $pb.packageSchema -ne $Schema.package -or $pb.startupSchema -ne $Schema.startup -or
            $pb.comparisonSchema -ne $Schema.comparison) { Fail 'playback is not an enabled capability of this install' }
        # The package Wheelkit prepared: both seals as named, this exe, eligible for playback.
        $d = Read-Json "$Recording\recording.json"
        if ($null -eq $d -or $d.schema -ne $Schema.package -or $d.playback -ne $true) { Fail 'the recording is not a playable package (recording-package@2, playback true)' }
        if ($d.recordingSealSha256 -cne $RecordingSealSha256 -or $d.startupSealSha256 -cne $StartupSealSha256) { Fail 'the recording is not the one prepared (its seals differ)' }
        if ($d.gameBuildSha256 -cne $exeSha) { Fail 'the recording was made by another OutRun executable' }
        if ((Get-Sha "$Recording\capture\recording-complete.tsv") -cne $RecordingSealSha256) { Fail 'the recording seal has changed' }
        if ((Get-Sha "$Recording\startup\complete.tsv") -cne $StartupSealSha256) { Fail 'the startup seal has changed' }
        $proof = Read-Json "$Recording\capture\recorder-complete.json"
        if ($null -eq $proof -or $proof.PSObject.Properties['action'] -and $proof.action -ne 'record') { Fail 'the package is not an ordinary recording' }
        $recordingRequestId = "$($proof.requestId)"
        if ($recordingRequestId -ceq $RequestId) { Fail 'playback needs its own request identity' }
        $rows = [uint64]$proof.report.written
        Test-Quiet $exePath

        # The owner's files now (never written by a playback; checked unchanged at the end), and the admitted tape.
        $ownerBefore = Get-OwnerFingerprint $GameDir
        foreach ($p in (Split-Path $Result)) { New-Item -ItemType Directory -Force -Path $p | Out-Null }
        New-Item -ItemType Directory -Path $Result | Out-Null; $ownResult = $true   # only now is the folder this invocation's
        Write-New "$Result\owner-before.json" ($ownerBefore | ConvertTo-Json)
        $tape = "$Result\tape.txt"
        $out = & $finalizer --export-playback "$Recording\capture" $recordingRequestId $exeSha "$($proof.processId)" "$($proof.processCreationFileTime)" $tape 2>&1
        $code = $LASTEXITCODE; ($out | Out-String) | Set-Content -LiteralPath "$Result\export.log"
        if ($code -ne 0 -or -not (Test-Path -LiteralPath $tape)) { Fail "the recording is not admitted for playback (export exit $code; export.log)" }
        $tapeSha = Get-Sha $tape

        # The request and the launch, muted for the whole child (it opens no device either).
        $expires = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 120
        $requestText = "$($Schema.request)`r`nrequestId=$RequestId`r`naction=playback`r`nexpiresUnix=$expires`r`nresult=$Result`r`nrecording=$Result\replay`r`n" +
            "tape=$tape`r`ntapeSha256=$tapeSha`r`npackageSealSha256=$RecordingSealSha256`r`nexeSha256=$exeSha`r`nstartup=$Recording\startup`r`nstartupSealSha256=$StartupSealSha256`r`n"
        Write-New $requestFile $requestText
        $psi = [System.Diagnostics.ProcessStartInfo]::new($exePath); $psi.WorkingDirectory = $GameDir; $psi.UseShellExecute = $false
        $psi.Environment['DBCE_FFB_MUTE'] = '1'
        if ($TestFault -eq 'launch') { Fail 'the launch failed (test fault)' }
        $proc = [System.Diagnostics.Process]::Start($psi)
        $processId = [uint64]$proc.Id; $creation = Get-Creation $proc.Handle   # the handle is held: this pid cannot be another process
        if ($null -eq $creation) { Fail 'the launched process has no creation time' }
        Write-Replace "$Result\companion.json" ([ordered]@{ schema = $Schema.companion; requestId = $RequestId; action = 'playback'; processId = $processId
            processCreationFileTime = $creation; exe = $exePath; exeSha256 = $exeSha; startedUtc = $startedUtc } | ConvertTo-Json)
        Write-Output "launched $Exe process $processId; playback $RequestId of $recordingRequestId ($rows rows)"

        # Admission: this process takes the request.
        $deadline = [DateTime]::UtcNow.AddSeconds($AdmissionSeconds); $s = $null
        while (-not $proc.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            $s = Get-OwnStatus $Result $RequestId $processId $creation
            if ($s -is [string]) { $state.why += 'a status.json naming another process was ignored' } elseif ($null -ne $s) { break }
            Start-Sleep -Milliseconds $PollMilliseconds
        }
        if ($null -ne $s -and $s -isnot [string]) {
            $state.admitted = $true; Write-Output "admitted: $($s.state)"
            if ($s.state -eq 'failed') { $state.why += "the game refused the playback: $($s.why)" }
        } else { $state.why += 'the game did not take the request' }
        if ((Test-Path -LiteralPath $requestFile) -and [System.IO.File]::ReadAllText($requestFile) -ceq $requestText) {
            Move-Item -LiteralPath $requestFile "$Result\session-request.not-taken.txt"; $state.why += 'the request was set aside unread' }

        # Supervision, bounded by the tape (run.py's allowance: rows / 30 + 60 s): at complete seal and compare, then close.
        $bound = [DateTime]::UtcNow.AddSeconds([double]$rows / 30 + 60); $replay = "$Result\replay\capture"; $done = $false
        while ($state.admitted -and -not $proc.HasExited -and -not $done) {
            $s = Get-OwnStatus $Result $RequestId $processId $creation
            if ($s -and $s -isnot [string]) { $state.finalState = $s.state; if ($s.stoppedBy) { $state.stoppedBy = $s.stoppedBy } }
            if ((Test-Path -LiteralPath "$Result\cancel.request") -and (Read-Shared "$Result\cancel.request") -ceq $RequestId) {
                $state.cancelled = $true; $state.why += 'cancelled (-Stop)'; break }
            if ($state.finalState -eq 'failed') { $state.why += "the playback capture failed: $($s.why)"; break }
            if ($state.finalState -in 'complete', 'saved') {
                if (-not $state.sealed) {
                    $out = & $finalizer --seal-recording $replay $RequestId $exeSha "$processId" "$creation" "$Result\recording-verified.json" 2>&1
                    $code = $LASTEXITCODE; ($out | Out-String) | Set-Content -LiteralPath "$Result\seal.log"
                    if ($code -ne 0) { $state.why += "the managed seal refused the replay capture (exit $code; seal.log)"; break }
                    $state.sealed = $true
                }
                $out = & $finalizer --compare-recordings "$Recording\capture" $replay "$Result\comparison.json" 2>&1
                $code = $LASTEXITCODE; ($out | Out-String) | Set-Content -LiteralPath "$Result\compare.log"
                $state.comparisonCode = $code
                if (Test-Path -LiteralPath "$Result\comparison.json") { $state.compared = $true; $state.comparisonSha256 = Get-Sha "$Result\comparison.json" }
                if ($code -eq 2) { $state.why += 'the game diverged from the recording (comparison.json names the first row and channel)' }
                elseif ($code -ne 0) { $state.why += "the comparison is not valid evidence (exit $code; compare.log)" }
                $done = $true; break
            }
            if ([DateTime]::UtcNow -gt $bound) { $state.why += "the playback did not finish within $([int]([double]$rows / 30 + 60)) s"; break }
            Start-Sleep -Milliseconds $PollMilliseconds
        }
        if ($state.admitted -and -not $done -and -not $state.cancelled -and $state.finalState -notin 'complete', 'saved') { $state.why += "the playback ended $($state.finalState)" }
    } catch {
        $state.why += "companion: $($_.Exception.Message)"
        if ($proc) { Write-Output "error while the game runs: $($_.Exception.Message); the game is closed normally" }
    }
    # Common cleanup, after a normal end and after an error alike (sana 38426). This invocation's own request is never left
    # to be taken by a later launch (only its exact bytes are moved aside).
    if ($requestText -and (Test-Path -LiteralPath $requestFile) -and [System.IO.File]::ReadAllText($requestFile) -ceq $requestText) {
        Move-Item -LiteralPath $requestFile "$Result\session-request.not-taken.txt"; $state.why += 'the request was set aside unread' }
    # No child: a refusal. It writes a result only into the folder this invocation made (sana 38347): an existing one is
    # left as it was.
    if (-not $proc) { Write-Output "refused: $((@($state.why)[-1]) -replace '^companion: ', '')"; if ($ownResult) { [void](Write-PlaybackResult) }; exit 1 }
    # The exact child, closed normally (it saves nothing of the owner's); never killed. One that stays keeps the result pending.
    if (-not $proc.HasExited) {
        $posted = [OutRunSession.Native]::CloseWindowsOf([uint32]$processId)
        if (-not $proc.WaitForExit($CloseSeconds * 1000)) { $state.why += "the game did not close within $CloseSeconds s of WM_CLOSE ($posted window(s)); it is left running" }
    }
    if ($proc.HasExited) { $state.closed = $true; $state.exitCode = $proc.ExitCode; if ($state.exitCode -ne 0) { $state.why += "the game exited with code $($state.exitCode)" } }
    # The owner's files: unchanged, or the difference reported (never copied over).
    if ($null -ne $ownerBefore -and $state.closed) {
        try {
            $after = Get-OwnerFingerprint $GameDir
            $changed = @(@($ownerBefore.Keys) + @($after.Keys) | Sort-Object -Unique | Where-Object { $ownerBefore[$_] -ne $after[$_] })
            $state.ownerStateUnchanged = $changed.Count -eq 0
            if ($changed.Count) { $state.why += "the owner's files changed during the playback: $($changed -join ', ') (reported, not repaired)" }
        } catch { $state.why += "the owner's files could not be checked: $($_.Exception.Message)" }
    }
    $r = Write-PlaybackResult
    Write-Output ("result: $($r.state), passed $($r.passed), comparison exit $($state.comparisonCode), closed $($state.closed), owner files unchanged $($state.ownerStateUnchanged)" +
        $(if ($state.why.Count) { '; ' + ($state.why -join '; ') } else { '' }))
    if ($r.passed) { exit 0 }
    if ($r.state -eq 'complete' -and $state.comparisonCode -eq 2) { exit 2 }
    exit 1
}

# ---- Record ----
$state = [ordered]@{ admitted = $false; sealed = $false; saved = $false; descriptor = $false; readBack = $false; finalState = $null; stoppedBy = $null
                     rows = $null; exitCode = $null; closed = $false; why = @() }
$proc = $null; $processId = 0; $creation = 0; $startedUtc = [DateTime]::UtcNow.ToString('o'); $requestText = $null
$startupSeal = $null; $startupWhy = $null; $ownResult = $false
function Write-Result {
    # A pass needs the descriptor too (sana 38466): without recording.json Wheelkit cannot list the capture, which stays
    # saved (a valid raw capture) but is not a passed session.
    $passed = $state.saved -and $state.readBack -and $state.descriptor -and $state.closed -and $state.exitCode -eq 0
    $r = [ordered]@{ schema = $Schema.result; requestId = $RequestId; action = 'record'; recording = $Recording; result = $Result
                     processId = $processId; processCreationFileTime = $creation; startedUtc = $startedUtc; completedUtc = [DateTime]::UtcNow.ToString('o')
                     admitted = $state.admitted; sealed = $state.sealed; saved = $state.saved; descriptor = $state.descriptor; readBack = $state.readBack
                     finalState = $state.finalState; stoppedBy = $state.stoppedBy; rows = $state.rows; limits = $Limits
                     closed = $state.closed; exitCode = $state.exitCode; passed = $passed; why = @($state.why) }
    Write-Replace "$Result\result.json" ($r | ConvertTo-Json -Depth 4)
    return $passed
}
try {
    # Preflight: refuse before anything is written.
    if (-not $Recording) { Fail '-Record needs -Recording' }
    $Recording = [System.IO.Path]::GetFullPath($Recording)
    foreach ($p in $Recording, $Result) {
        if (Test-Path -LiteralPath $p) { Fail "$p exists (a new session needs new folders)" }
        if (Test-Within $p $GameDir) { Fail "$p is inside the game folder" }
    }
    if ((Test-Within $Recording $Result) -or (Test-Within $Result $Recording)) { Fail 'the recording and result folders must be separate' }
    if (-not (Test-Path -LiteralPath $exePath)) { Fail "no $Exe in $GameDir" }
    $install = Get-Install $exePath; $exeSha = $install.exeSha; $finalizer = $install.finalizer; $cap = $install.cap
    if (-not $cap.record.enabled) { Fail 'recording is not an enabled capability of this install' }
    # [Input] Backend = Shim is the owner's own choice; Record never changes it (sana 34164).
    $ini = Join-Path $GameDir 'OutRunDesktop.ini'
    $section = $null; $backend = $null
    foreach ($line in [System.IO.File]::ReadAllLines($ini)) {
        $t = $line.Trim()
        if ($t -match '^\[(.+)\]$') { $section = $Matches[1]; continue }
        if ($section -eq 'Input' -and $t -match '^Backend\s*=\s*(.*)$') { $backend = $Matches[1].Trim() }
    }
    if ($backend -ne 'Shim') { Fail 'Play with Recording needs [Input] Backend = Shim in OutRunDesktop.ini (the owner chooses it)' }
    Test-Quiet $exePath

    # The startup snapshot, with the game closed (the owner's files only read). A failure leaves the recording for
    # analysis only, with the reason; the snapshot folder is kept aside as it was, never deleted.
    foreach ($p in (Split-Path $Recording), (Split-Path $Result)) { New-Item -ItemType Directory -Force -Path $p | Out-Null }
    New-Item -ItemType Directory -Path $Recording | Out-Null
    try { $startupSeal = New-StartupSnapshot $GameDir "$Recording\startup" $RequestId $exeSha; Write-Output "startup state sealed ($startupSeal)" }
    catch {
        $startupWhy = "the startup state could not be captured: $($_.Exception.Message)"; $state.why += $startupWhy
        if (Test-Path -LiteralPath "$Recording\startup") { Move-Item -LiteralPath "$Recording\startup" "$Recording\startup.incomplete" }
    }

    # The request and the launch.
    New-Item -ItemType Directory -Path $Result | Out-Null; $ownResult = $true   # only now is the folder this invocation's
    $expires = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 120
    $requestText = "$($Schema.request)`r`nrequestId=$RequestId`r`naction=record`r`nexpiresUnix=$expires`r`nresult=$Result`r`nrecording=$Recording`r`n"
    Write-New $requestFile $requestText
    $psi = [System.Diagnostics.ProcessStartInfo]::new($exePath); $psi.WorkingDirectory = $GameDir; $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
    $processId = [uint64]$proc.Id; $creation = Get-Creation $proc.Handle   # the handle is held: this pid cannot be another process
    if ($null -eq $creation) { Fail 'the launched process has no creation time' }
    Write-Replace "$Result\companion.json" ([ordered]@{ schema = $Schema.companion; requestId = $RequestId; action = 'record'; processId = $processId
        processCreationFileTime = $creation; exe = $exePath; exeSha256 = $exeSha; startedUtc = $startedUtc } | ConvertTo-Json)
    Write-Output "launched $Exe process $processId; request $RequestId"

    # Admission: this process takes the request.
    $deadline = [DateTime]::UtcNow.AddSeconds($AdmissionSeconds); $s = $null
    while (-not $proc.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        $s = Get-OwnStatus $Result $RequestId $processId $creation
        if ($s -is [string]) { $state.why += 'a status.json naming another process was ignored' }
        elseif ($null -ne $s) { break }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    if ($null -ne $s -and $s -isnot [string]) {
        $state.admitted = $true; Write-Output "admitted: $($s.state)"
        if ($s.state -eq 'failed') { $state.why += "the game refused the recording: $($s.why)" }
    } else {
        $state.why += 'the game did not take the request'
    }
    # A request this game did not take never waits for a later launch (only these exact bytes are moved aside).
    if ((Test-Path -LiteralPath $requestFile) -and [System.IO.File]::ReadAllText($requestFile) -ceq $requestText) {
        Move-Item -LiteralPath $requestFile "$Result\session-request.not-taken.txt"; $state.why += 'the request was set aside unread' }

    # Supervision: seal at complete while the game runs; wait for the owner's close (no timeout, never a close).
    $receipt = "$Result\recording-verified.json"; $capture = "$Recording\capture"; $sealTried = $false
    while ($true) {
        $exited = $proc.HasExited
        if ($state.admitted) {
            $s = Get-OwnStatus $Result $RequestId $processId $creation
            if ($s -is [string]) {   # never this session's: ignored (and noted once) until the game's own status returns
                if ($state.why -notcontains 'a status.json naming another process was ignored') { $state.why += 'a status.json naming another process was ignored' } }
            elseif ($s) { $state.finalState = $s.state; if ($s.stoppedBy) { $state.stoppedBy = $s.stoppedBy } }
            if (-not $sealTried -and $state.finalState -in 'complete', 'saved') {
                $sealTried = $true
                $out = & $finalizer --seal-recording $capture $RequestId $exeSha "$processId" "$creation" $receipt 2>&1
                $code = $LASTEXITCODE
                ($out | Out-String) | Set-Content -LiteralPath "$Result\seal.log"
                if ($code -ne 0) { $state.why += "the managed seal refused the capture (exit $code; seal.log)" }
                else { $state.sealed = $true }
                # The receipt must name this request, this process, this exe, the capture's proof and its seal.
                $rc = Read-Json $receipt; $proof = Read-Json "$capture\recorder-complete.json"
                if ($state.sealed -and $rc -and $proof -and $rc.schema -eq $Schema.receipt -and $rc.requestId -ceq $RequestId -and [uint64]$rc.processId -eq $processId -and
                    [uint64]$rc.processCreationFileTime -eq $creation -and $rc.exeSha256 -ceq $exeSha -and $rc.recordingSaved -eq $true -and
                    $rc.artifactSha256 -ceq $proof.artifactSha256 -and $rc.sealSha256 -ceq (Get-Sha "$capture\recording-complete.tsv")) {
                    # Read back at once (a new receipt; the seal is never rewritten), so a later crash keeps a verified recording.
                    $out = & $finalizer --read-recording $capture $RequestId $exeSha "$processId" "$creation" "$Result\recording-reread.json" 2>&1
                    $code = $LASTEXITCODE
                    ($out | Out-String) | Set-Content -LiteralPath "$Result\read.log"
                    if ($code -ne 0) { $state.why += "the saved recording did not read back (exit $code; read.log)" }
                    else {
                        $state.readBack = $true; $state.saved = $true; $state.rows = [uint64]$proof.report.written
                        # Playback eligibility: a sealed startup state, and the capture admitted for playback by the managed
                        # finalizer (finite input and pose rows, a moved active car; sana 37707). Otherwise analysis only.
                        $playable = $false; $playbackWhy = $startupWhy
                        if ($startupSeal) {
                            $out = & $finalizer --read-playback-recording $capture $RequestId $exeSha "$processId" "$creation" "$Result\playback-admission.json" 2>&1
                            $code = $LASTEXITCODE
                            ($out | Out-String) | Set-Content -LiteralPath "$Result\playback-admission.log"
                            if ($code -eq 0) { $playable = $true } else { $playbackWhy = "the capture is not admitted for playback (exit $code; playback-admission.log)" }
                        }
                        # The library's entry (sana 34447/34458), beside the capture, never in it: display context only; the
                        # native seal and report stay the identity and integrity.
                        # The proof's own startedUtc text: ConvertFrom-Json turns dates into DateTime, whose text is not the native one.
                        $started = if ((Read-Shared "$capture\recorder-complete.json") -match '"startedUtc"\s*:\s*"([^"]+)"') { $Matches[1] } else { Fail 'the proof has no startedUtc' }
                        $d = [ordered]@{ schema = $Schema.package; gameId = 'outrun-2006-decomp'; requestId = $RequestId; recordedUtc = $started
                            inputOrigin = $InputOrigin; recordingSealSha256 = $rc.sealSha256; startupSealSha256 = $startupSeal; rows = $state.rows
                            durationUs = (Get-DurationUs "$capture\source.jsonl"); stoppedBy = $proof.stoppedBy; gameBuildSha256 = $exeSha; playback = $playable }
                        if (-not $playable) { $d['playbackWhy'] = $playbackWhy }
                        Write-New "$Recording\recording.json" ($d | ConvertTo-Json); $state.descriptor = $true
                        Write-Output "recording saved: $($state.rows) rows; playback $(if ($playable) { 'available' } else { "not available ($playbackWhy)" })"
                    }
                } elseif ($state.sealed) { $state.why += 'the receipt does not name this request, process, exe, proof and seal' }
            }
        }
        if ($exited) { break }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    $proc.WaitForExit(); $state.closed = $true; $state.exitCode = $proc.ExitCode
    if ($state.exitCode -ne 0) { $state.why += "the game exited with code $($state.exitCode)" }
    if ($state.admitted -and $state.finalState -notin 'complete', 'saved') { $state.why += "the recording ended $($state.finalState)" }
} catch {
    $state.why += "companion: $($_.Exception.Message)"
    # A refusal writes a result only into the folder this invocation made (sana 38347): an existing one is left as it was.
    if (-not $proc) { Write-Output "refused: $($_.Exception.Message)"; if ($ownResult) { [void](Write-Result) }; exit 1 }
    if (-not $proc.HasExited) { Write-Output "error while the game runs: $($_.Exception.Message); the game is left running" }
    else { $state.closed = $true; $state.exitCode = $proc.ExitCode }
}
$passed = Write-Result
Write-Output ("result: saved $($state.saved), closed $($state.closed), exit code $($state.exitCode), passed $passed" + $(if ($state.why.Count) { '; ' + ($state.why -join '; ') } else { '' }))
if ($passed) { exit 0 }
if ($state.saved) { exit 2 }
exit 1
