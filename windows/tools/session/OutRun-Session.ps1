# OutRun 2006 (decomp): the Play with Recording companion that Wheelkit runs (sana 34164/34207; docs/PLAY-WITH-RECORDING.md
# "The companion"). It mirrors DRIVE's full-session bridge: one owned session per request, never input, never a close.
#
#   OutRun-Session.ps1 -Record -GameDir <install> -Recording <package-root> -Result <run-root> -RequestId <32 hex>
#   OutRun-Session.ps1 -Stop   -GameDir <install> -Result <run-root> -RequestId <32 hex>
#
# -Record: preflight (the install record owns this exe, this companion, the finalizer and record-enabled capabilities;
#   [Input] Backend = Shim is the owner's own choice in the INI; no inject.on or other request; the game not running),
#   the native one-shot request, the launch. The process identity (pid + creation FILETIME) is pinned from the launched
#   process itself, and status.json counts only when it names that process. At complete, the managed finalizer seals
#   the capture while the game runs; the descriptor recording.json goes beside the capture once the receipt checks out.
#   Then it waits for the owner to close the game (no timeout), reads the recording back and writes result.json.
#   Saved and a clean close are reported apart: an abnormal exit is never a pass, and never removes a saved recording.
# -Stop: the request-bound stop (<result>\stop.request with the id), only for the live process this companion launched.
# Exit codes: 0 pass (Record) / stop requested (Stop); 1 refused or failed; 2 saved but not a clean close.
[CmdletBinding()]
param(
    [switch]$Record, [switch]$Stop,
    [Parameter(Mandatory)][string]$GameDir,
    [string]$Recording,
    [Parameter(Mandatory)][string]$Result,
    [Parameter(Mandatory)][string]$RequestId,
    [ValidateSet('human', 'automated', 'unspecified')][string]$InputOrigin = 'unspecified',   # Wheelkit: human; runners: automated
    [string]$Exe = 'OutRunDesktop.exe',          # test seam: the device-free fixture (tests/session_companion)
    [int]$AdmissionSeconds = 60,
    [int]$PollMilliseconds = 250
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$Schema = @{ companion = 'outrun.companion-session@1'; result = 'outrun.full-session-result@1'; package = 'outrun.recording-package@1'
             status = 'outrun.session-status@1'; receipt = 'outrun.recording-verified@1'; request = 'outrun.session-request@1' }
# The native side's current limits (ordinary_recording.hpp / shim_input.cpp): reported as limits, never as a run's length.
$Limits = [ordered]@{ rows = 18000; spanSeconds = 299; hz = 60; note = 'current limits of the native recorder, not a demonstrated duration' }

Add-Type -Namespace OutRunSession -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)] public static extern System.IntPtr OpenProcess(uint access, bool inherit, uint processId);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool GetProcessTimes(System.IntPtr process, out long creation, out long exit, out long kernel, out long user);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr handle);
[DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(System.IntPtr process, out uint code);
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
$fence = Join-Path $env:LOCALAPPDATA 'dbce\outrun2006-decomp'
$requestFile = Join-Path $fence 'session-request.txt'

if ($Record -eq $Stop) { Write-Output 'refused: exactly one of -Record and -Stop'; exit 1 }
if (-not (Test-Id $RequestId)) { Write-Output 'refused: -RequestId must be 32 lower-case hex digits'; exit 1 }
$GameDir = [System.IO.Path]::GetFullPath($GameDir); $Result = [System.IO.Path]::GetFullPath($Result)
$exePath = Join-Path $GameDir $Exe

if ($Stop) {
    # Bound to the request and the exact live process this companion launched; it asks the game to save, nothing else.
    $c = Read-Json "$Result\companion.json"
    if ($null -eq $c -or $c.schema -ne $Schema.companion -or $c.requestId -cne $RequestId) { Write-Output 'refused: no session of this request was started here'; exit 1 }
    $processId = [uint64]$c.processId; $creation = [uint64]$c.processCreationFileTime
    if (-not (Test-Alive $processId $creation)) { Write-Output 'refused: the game this session launched is not running'; exit 1 }
    $s = Get-OwnStatus $Result $RequestId $processId $creation
    if ($null -eq $s -or $s -is [string]) { Write-Output 'refused: the game has not acknowledged this recording'; exit 1 }
    if ($s.state -ne 'recording') { Write-Output "not sent: the recording is already $($s.state)"; exit 1 }
    Write-Replace "$Result\stop.request" $RequestId
    Write-Output "stop requested for $RequestId (process $processId); the game saves without quitting"
    exit 0
}

# ---- Record ----
$state = [ordered]@{ admitted = $false; sealed = $false; saved = $false; descriptor = $false; readBack = $false; finalState = $null; stoppedBy = $null
                     rows = $null; exitCode = $null; closed = $false; why = @() }
$proc = $null; $processId = 0; $creation = 0; $startedUtc = [DateTime]::UtcNow.ToString('o'); $requestText = $null
function Write-Result {
    $passed = $state.saved -and $state.readBack -and $state.closed -and $state.exitCode -eq 0
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
    $exeSha = Get-Sha $exePath
    # The install record owns this exe, this companion, the finalizer and the capabilities (DRIVE's receipt rule).
    # Selected by the whole payload it owns (exe, this companion, the capabilities) before the ambiguity check, so an
    # earlier record naming the same exe (a companion-only update) never disables recording (sana 34661).
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
    $finalizer = Join-Path $finDir 'P21Finalizer.exe'
    foreach ($required in 'P21Finalizer.exe', 'P21Finalizer.dll', 'Dbce.Wheel.Recording.dll') {   # as Wheelkit's Capability (sana d7d6084)
        if (-not $session.finalizer.PSObject.Properties[$required]) { Fail "the install record does not own $required" } }
    $cap = Get-Content -LiteralPath $capPath -Raw | ConvertFrom-Json   # its bytes are the selected record's (above)
    if ($cap.schema -ne 'dbce.session-capabilities@1' -or $cap.protocol -ne $Schema.request -or "$($cap.gameBuildSha256)".ToLowerInvariant() -ne $exeSha -or
        -not $cap.record.enabled) { Fail 'recording is not an enabled capability of this install' }
    # [Input] Backend = Shim is the owner's own choice; Record never changes it (sana 34164).
    $ini = Join-Path $GameDir 'OutRunDesktop.ini'
    $section = $null; $backend = $null
    foreach ($line in [System.IO.File]::ReadAllLines($ini)) {
        $t = $line.Trim()
        if ($t -match '^\[(.+)\]$') { $section = $Matches[1]; continue }
        if ($section -eq 'Input' -and $t -match '^Backend\s*=\s*(.*)$') { $backend = $Matches[1].Trim() }
    }
    if ($backend -ne 'Shim') { Fail 'Play with Recording needs [Input] Backend = Shim in OutRunDesktop.ini (the owner chooses it)' }
    New-Item -ItemType Directory -Force -Path $fence | Out-Null
    $stray = @(Get-ChildItem -LiteralPath $fence -Force | Where-Object { $_.Name -like 'session-request.txt*' -or $_.Name -eq 'inject.on' })
    if ($stray.Count) { Fail "the game's request folder already holds $($stray.Name -join ', ')" }
    $running = @(Get-CimInstance Win32_Process -Filter "Name='$Exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ExecutablePath -and $_.ExecutablePath -ieq $exePath })
    if ($running.Count) { Fail "$Exe is already running from $GameDir" }

    # The request and the launch.
    foreach ($p in (Split-Path $Recording), (Split-Path $Result)) { New-Item -ItemType Directory -Force -Path $p | Out-Null }
    New-Item -ItemType Directory -Path $Result | Out-Null
    $expires = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 120
    $requestText = "$($Schema.request)`r`nrequestId=$RequestId`r`naction=record`r`nexpiresUnix=$expires`r`nresult=$Result`r`nrecording=$Recording`r`n"
    Write-New $requestFile $requestText
    $psi = [System.Diagnostics.ProcessStartInfo]::new($exePath); $psi.WorkingDirectory = $GameDir; $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
    $processId = [uint64]$proc.Id; $creation = Get-Creation $proc.Handle   # the handle is held: this pid cannot be another process
    if ($null -eq $creation) { Fail 'the launched process has no creation time' }
    Write-Replace "$Result\companion.json" ([ordered]@{ schema = $Schema.companion; requestId = $RequestId; processId = $processId
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
                        # The library's entry (sana 34447/34458), beside the capture, never in it: display context only; the
                        # native seal and report stay the identity and integrity.
                        # The proof's own startedUtc text: ConvertFrom-Json turns dates into DateTime, whose text is not the native one.
                        $started = if ((Read-Shared "$capture\recorder-complete.json") -match '"startedUtc"\s*:\s*"([^"]+)"') { $Matches[1] } else { Fail 'the proof has no startedUtc' }
                        $d = [ordered]@{ schema = $Schema.package; gameId = 'outrun-2006-decomp'; requestId = $RequestId; recordedUtc = $started
                            inputOrigin = $InputOrigin; recordingSealSha256 = $rc.sealSha256; rows = $state.rows
                            durationUs = (Get-DurationUs "$capture\source.jsonl"); stoppedBy = $proof.stoppedBy; gameBuildSha256 = $exeSha; playback = $false }
                        Write-New "$Recording\recording.json" ($d | ConvertTo-Json); $state.descriptor = $true
                        Write-Output "recording saved: $($state.rows) rows"
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
    if (-not $proc) { Write-Output "refused: $($_.Exception.Message)"; if (Test-Path -LiteralPath $Result) { [void](Write-Result) }; exit 1 }
    if (-not $proc.HasExited) { Write-Output "error while the game runs: $($_.Exception.Message); the game is left running" }
    else { $state.closed = $true; $state.exitCode = $proc.ExitCode }
}
$passed = Write-Result
Write-Output ("result: saved $($state.saved), closed $($state.closed), exit code $($state.exitCode), passed $passed" + $(if ($state.why.Count) { '; ' + ($state.why -join '; ') } else { '' }))
if ($passed) { exit 0 }
if ($state.saved) { exit 2 }
exit 1
