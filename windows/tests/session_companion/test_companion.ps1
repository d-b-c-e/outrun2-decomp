# Device-free fixtures for windows/tools/session/OutRun-Session.ps1 (the Play with Recording companion; sana 34207):
# the real companion, the real managed finalizer and session_game_fixture.exe (the game's own intake and recorder with
# synthetic rows). Every case has its own install folder, sessions and a private %LOCALAPPDATA% (the fixtures never
# touch the real request folder); nothing here launches the game, opens a device or sends input.
#   pwsh -File test_companion.ps1 -Fixture <build>\session_game_fixture.exe -Finalizer <P21Finalizer build folder> -Out <new folder>
param([Parameter(Mandatory)][string]$Fixture, [Parameter(Mandatory)][string]$Finalizer, [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
$companion = (Resolve-Path "$PSScriptRoot\..\..\tools\session\OutRun-Session.ps1").Path
if (Test-Path $Out) { throw "$Out exists" }
New-Item -ItemType Directory $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$local = "$Out\localappdata"; $fence = "$local\dbce\outrun2006-decomp"
New-Item -ItemType Directory $fence -Force | Out-Null
$script:checks = 0; $script:failed = @()
function Check([bool]$ok, [string]$what) { $script:checks++; if (-not $ok) { $script:failed += $what; Write-Output "FAIL: $what" } }
function New-Id { $b = [byte[]]::new(16); [System.Security.Cryptography.RandomNumberGenerator]::Fill($b); -join ($b | ForEach-Object { $_.ToString('x2') }) }
function Sha([string]$p) { (Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant() }
function Read-Json([string]$p) { try { $f = [System.IO.File]::Open($p, 'Open', 'Read', 'ReadWrite, Delete'); try { ([System.IO.StreamReader]::new($f)).ReadToEnd() | ConvertFrom-Json } finally { $f.Dispose() } } catch { $null } }
function Wait-Until([scriptblock]$cond, [int]$seconds) { $t = [DateTime]::UtcNow.AddSeconds($seconds); while ([DateTime]::UtcNow -lt $t) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 100 }; return [bool](& $cond) }
# An install of the fixture as the owner package would lay it out: exe, INI, finalizer folder, capabilities, install record.
function New-Game([string]$case, [string]$backend = 'Shim', [bool]$recordEnabled = $true, [switch]$TamperFinalizer, [string]$Options = '',
                  [switch]$OlderRecord, [switch]$DuplicateRecord) {
    $g = "$Out\$case\game"; New-Item -ItemType Directory $g -Force | Out-Null
    Copy-Item $Fixture "$g\session_game_fixture.exe"
    [System.IO.File]::WriteAllText("$g\OutRunDesktop.ini", "[Input]`r`nBackend = $backend`r`n")
    if ($Options) { [System.IO.File]::WriteAllText("$g\fixture-options.txt", $Options) }
    $fd = "$g\recording-finalizer"; New-Item -ItemType Directory $fd | Out-Null
    Get-ChildItem $Finalizer -File | Copy-Item -Destination $fd
    $map = [ordered]@{}; foreach ($f in Get-ChildItem $fd -File | Sort-Object Name) { $map[$f.Name] = Sha $f.FullName }
    if ($TamperFinalizer) { [System.IO.File]::AppendAllText("$fd\P21Finalizer.runtimeconfig.json", ' ') }
    $exeSha = Sha "$g\session_game_fixture.exe"
    $cap = [ordered]@{ schema = 'dbce.session-capabilities@1'; game = 'OutRun 2006 (decomp)'; protocol = 'outrun.session-request@1'; gameBuildSha256 = $exeSha
                       runner = 'OutRun-Session.ps1'; record = @{ enabled = $recordEnabled }; playback = @{ enabled = $false; adapters = @() } }
    [System.IO.File]::WriteAllText("$g\session-capabilities.json", ($cap | ConvertTo-Json -Depth 4))
    $rec = [ordered]@{ schema = 'outrun.owner-install@1'; tag = 'fixture'; state = 'installed'; installedExeSha256 = $exeSha.ToUpperInvariant()
                       session = [ordered]@{ companionSha256 = (Sha $companion); capabilitiesSha256 = (Sha "$g\session-capabilities.json")
                                             finalizerDirectory = 'recording-finalizer'; finalizer = $map } }
    [System.IO.File]::WriteAllText("$g\OutRunDesktop.install-fixture.json", ($rec | ConvertTo-Json -Depth 5))
    # A record from before a companion-only update: the same exe, another companion (sana 34661). It must not count.
    if ($OlderRecord) { $old = $rec | ConvertTo-Json -Depth 5 | ConvertFrom-Json; $old.tag = 'fixture-older'; $old.session.companionSha256 = '0' * 64
                        [System.IO.File]::WriteAllText("$g\OutRunDesktop.install-fixture-older.json", ($old | ConvertTo-Json -Depth 5)) }
    # Two records owning the same whole payload: ambiguous, refused.
    if ($DuplicateRecord) { Copy-Item "$g\OutRunDesktop.install-fixture.json" "$g\OutRunDesktop.install-fixture-copy.json" }
    return $g
}
function Start-Companion([string]$log, [string[]]$arguments) {
    $psi = [System.Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    foreach ($a in @('-NoProfile', '-File', $companion) + $arguments) { $psi.ArgumentList.Add($a) }
    $psi.Environment['LOCALAPPDATA'] = $local; $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    return [pscustomobject]@{ process = $p; out = $o; err = $e; log = $log }
}
function Finish-Companion($c, [int]$seconds) {
    $done = $c.process.WaitForExit($seconds * 1000)
    if (-not $done) { $c.process.Kill($true); $c.process.WaitForExit() }
    $text = $c.out.Result + $c.err.Result; [System.IO.File]::WriteAllText($c.log, $text)
    return [pscustomobject]@{ code = $(if ($done) { $c.process.ExitCode } else { -1 }); text = $text }
}
function Run-Companion([string]$log, [string[]]$arguments, [int]$seconds = 120) { Finish-Companion (Start-Companion $log $arguments) $seconds }
function Fence-Clean { -not @(Get-ChildItem $fence -Force | Where-Object Name -like 'session-request.txt*').Count }
function Session([string]$case, [string]$id) { [pscustomobject]@{ rec = "$Out\$case\sessions\recordings\$id"; res = "$Out\$case\sessions\results\$id" } }
$common = @('-Exe', 'session_game_fixture.exe', '-PollMilliseconds', '100')

# 1. The owner's session: Record, a request-bound Stop, saved while running, a normal close.
& {
    $g = New-Game 'happy' -OlderRecord; $id = New-Id; $s = Session 'happy' $id   # an older record for the same exe does not count
    $c = Start-Companion "$Out\happy\record.log" (@('-Record', '-GameDir', $g, '-Recording', $s.rec, '-Result', $s.res, '-RequestId', $id, '-InputOrigin', 'automated') + $common)
    Check (Wait-Until { (Read-Json "$($s.res)\status.json").state -eq 'recording' } 30) 'happy: the game acknowledges the recording'
    $other = Run-Companion "$Out\happy\stop-other.log" (@('-Stop', '-GameDir', $g, '-Result', $s.res, '-RequestId', (New-Id)) + $common)
    Check ($other.code -eq 1 -and $other.text -match 'no session of this request') "happy: Stop for another request is refused ($($other.text.Trim()))"
    Check (-not (Test-Path "$($s.res)\stop.request")) 'happy: a refused Stop writes nothing'
    $stop = Run-Companion "$Out\happy\stop.log" (@('-Stop', '-GameDir', $g, '-Result', $s.res, '-RequestId', $id) + $common)
    Check ($stop.code -eq 0 -and (Get-Content "$($s.res)\stop.request" -Raw) -ceq $id) "happy: Stop writes the request-bound stop ($($stop.text.Trim()))"
    Check (Wait-Until { Test-Path "$($s.rec)\recording.json" } 60) 'happy: the descriptor appears while the game runs'
    Check ((Read-Json "$($s.res)\status.json").state -eq 'saved' -or (Wait-Until { (Read-Json "$($s.res)\status.json").state -eq 'saved' } 10)) 'happy: the game shows saved'
    [System.IO.File]::WriteAllText("$g\fixture-close.txt", '0')
    $r = Finish-Companion $c 90
    Check ($r.code -eq 0) "happy: the companion passes (exit $($r.code))"
    $j = Read-Json "$($s.res)\result.json"; $cj = Read-Json "$($s.res)\companion.json"; $st = Read-Json "$($s.res)\status.json"
    Check ($j.schema -eq 'outrun.full-session-result@1' -and $j.passed -and $j.saved -and $j.readBack -and $j.closed -and $j.exitCode -eq 0 -and $j.stoppedBy -eq 'stop-request') 'happy: result.json passed, saved, read back, closed, exit 0, stop-request'
    Check ($cj.processId -eq $st.processId -and "$($cj.processCreationFileTime)" -eq "$($st.processCreationFileTime)" -and $j.processId -eq $cj.processId) 'happy: the launched identity is the status identity'
    $d = Read-Json "$($s.rec)\recording.json"; $cap = "$($s.rec)\capture"
    Check ($d.schema -eq 'outrun.recording-package@1' -and $d.gameId -eq 'outrun-2006-decomp' -and $d.requestId -ceq $id -and $d.inputOrigin -eq 'automated' -and
           $d.recordingSealSha256 -ceq (Sha "$cap\recording-complete.tsv") -and $d.rows -gt 0 -and $d.durationUs -gt 0 -and $d.stoppedBy -eq 'stop-request' -and
           $d.gameBuildSha256 -ceq (Sha "$g\session_game_fixture.exe") -and $d.playback -eq $false) 'happy: the descriptor (sana 34458 shape)'
    Check (@($d.PSObject.Properties.Name) -join ',' -ceq 'schema,gameId,requestId,recordedUtc,inputOrigin,recordingSealSha256,rows,durationUs,stoppedBy,gameBuildSha256,playback') 'happy: the descriptor has exactly the agreed fields'
    $rawStart = { param($p) if ((Get-Content -LiteralPath $p -Raw) -match '"(?:recordedUtc|startedUtc)"\s*:\s*"([^"]+)"') { $Matches[1] } }
    Check ((& $rawStart "$($s.rec)\recording.json") -ceq (& $rawStart "$cap\recorder-complete.json")) 'happy: recordedUtc is the proof''s startedUtc text, byte for byte'
    Check (@(Get-ChildItem $cap).Count -eq 8 -and -not (Test-Path "$cap\recording.json")) 'happy: the capture holds the six, the proof and the seal; the descriptor is beside it'
    Check (Fence-Clean) 'happy: nothing left in the request folder'
    $late = Run-Companion "$Out\happy\stop-late.log" (@('-Stop', '-GameDir', $g, '-Result', $s.res, '-RequestId', $id) + $common)
    Check ($late.code -eq 1 -and $late.text -match 'not running') 'happy: Stop after the close is refused'
}

# 2. An abnormal end after the recording is saved: never a pass, and the saved recording stays.
& {
    $g = New-Game 'abnormal' -Options "crash-after-saved=3`r`n"; $id = New-Id; $s = Session 'abnormal' $id
    $c = Start-Companion "$Out\abnormal\record.log" (@('-Record', '-GameDir', $g, '-Recording', $s.rec, '-Result', $s.res, '-RequestId', $id) + $common)
    Check (Wait-Until { (Read-Json "$($s.res)\status.json").state -eq 'recording' } 30) 'abnormal: recording'
    [void](Run-Companion "$Out\abnormal\stop.log" (@('-Stop', '-GameDir', $g, '-Result', $s.res, '-RequestId', $id) + $common))
    $r = Finish-Companion $c 90
    $j = Read-Json "$($s.res)\result.json"
    Check ($r.code -eq 2) "abnormal: exit 2, saved but not a clean close (exit $($r.code))"
    Check ($j.saved -and $j.closed -and $j.exitCode -eq 3 -and -not $j.passed) 'abnormal: result.json saved, closed with code 3, not passed'
    Check ((Test-Path "$($s.rec)\recording.json") -and (Test-Path "$($s.rec)\capture\recording-complete.tsv")) 'abnormal: the saved recording and its descriptor stay'
}

# 3. A game that never takes the request: refused after the admission bound, the request set aside, the game left running.
& {
    $g = New-Game 'not-taken' -Options "ignore-request`r`n"; $id = New-Id; $s = Session 'not-taken' $id
    $c = Start-Companion "$Out\not-taken\record.log" (@('-Record', '-GameDir', $g, '-Recording', $s.rec, '-Result', $s.res, '-RequestId', $id, '-AdmissionSeconds', '3') + $common)
    Check (Wait-Until { Test-Path "$($s.res)\session-request.not-taken.txt" } 20) 'not-taken: the request is set aside after the bound'
    Check (Fence-Clean) 'not-taken: nothing left to take'
    Check (-not $c.process.HasExited) 'not-taken: the companion still supervises (it never closes the game)'
    [System.IO.File]::WriteAllText("$g\fixture-close.txt", '0')
    $r = Finish-Companion $c 60
    $j = Read-Json "$($s.res)\result.json"
    Check ($r.code -eq 1 -and -not $j.admitted -and -not $j.saved -and $j.closed -and $j.exitCode -eq 0) "not-taken: refused, closed normally (exit $($r.code))"
}

# 4. The capture limit: stopped by the recorder itself, saved, a normal close.
& {
    $g = New-Game 'limit' -Options "seconds=3`r`n"; $id = New-Id; $s = Session 'limit' $id
    $c = Start-Companion "$Out\limit\record.log" (@('-Record', '-GameDir', $g, '-Recording', $s.rec, '-Result', $s.res, '-RequestId', $id) + $common)
    Check (Wait-Until { Test-Path "$($s.rec)\recording.json" } 60) 'limit: saved without a Stop'
    [System.IO.File]::WriteAllText("$g\fixture-close.txt", '0')
    $r = Finish-Companion $c 90
    $d = Read-Json "$($s.rec)\recording.json"
    Check ($r.code -eq 0 -and $d.stoppedBy -eq 'limit' -and $d.durationUs -ge 1500000 -and $d.durationUs -le 2100000 -and $d.inputOrigin -eq 'unspecified') "limit: stopped by the limit, about 2 s, origin unspecified (exit $($r.code), $($d.durationUs) us)"
}

# 5. A status.json naming another process is never this session's (sana 34207): Stop is refused while it stands; the
#    game's own next status (its exit's save) is trusted again, sealed and saved.
& {
    $g = New-Game 'foreign-status'; $id = New-Id; $s = Session 'foreign-status' $id
    $c = Start-Companion "$Out\foreign-status\record.log" (@('-Record', '-GameDir', $g, '-Recording', $s.rec, '-Result', $s.res, '-RequestId', $id) + $common)
    Check (Wait-Until { (Read-Json "$($s.res)\status.json").state -eq 'recording' } 30) 'foreign-status: recording'
    $st = Read-Json "$($s.res)\status.json"; $st.processId = [uint64]$st.processId + 1
    $tmp = "$($s.res)\status.json.test"; [System.IO.File]::WriteAllText($tmp, ($st | ConvertTo-Json)); [System.IO.File]::Move($tmp, "$($s.res)\status.json", $true)
    $stop = Run-Companion "$Out\foreign-status\stop.log" (@('-Stop', '-GameDir', $g, '-Result', $s.res, '-RequestId', $id) + $common)
    Check ($stop.code -eq 1 -and $stop.text -match 'not acknowledged' -and -not (Test-Path "$($s.res)\stop.request")) "foreign-status: Stop refused ($($stop.text.Trim()))"
    Start-Sleep -Milliseconds 600
    Check (-not (Test-Path "$($s.res)\recording-verified.json")) 'foreign-status: nothing sealed on a foreign status'
    [System.IO.File]::WriteAllText("$g\fixture-close.txt", '0')
    $r = Finish-Companion $c 90
    $j = Read-Json "$($s.res)\result.json"
    Check ($r.code -eq 0 -and $j.saved -and $j.stoppedBy -eq 'exit' -and ($j.why -join ' ') -match 'another process') "foreign-status: saved by the exit once the game's own status returns, the foreign one noted (exit $($r.code))"
}

# 6. Refusals before anything is written or launched.
foreach ($t in @(
        @{ case = 'native-backend'; game = @{ backend = 'Native' }; want = 'Backend = Shim' },
        @{ case = 'record-disabled'; game = @{ recordEnabled = $false }; want = 'not an enabled capability' },
        @{ case = 'finalizer-tampered'; game = @{ TamperFinalizer = $true }; want = 'is not the installed one' },
        @{ case = 'ambiguous-records'; game = @{ DuplicateRecord = $true }; want = 'no single install record' },
        @{ case = 'stray-request'; game = @{}; want = 'already holds'; stray = $true },
        @{ case = 'result-exists'; game = @{}; want = 'exists'; resultExists = $true })) {
    $p = $t.game; $g = New-Game $t.case @p; $id = New-Id; $s = Session $t.case $id
    if ($t.stray) { [System.IO.File]::WriteAllText("$fence\session-request.txt", 'someone else') }
    if ($t.resultExists) { New-Item -ItemType Directory $s.res -Force | Out-Null }
    $r = Run-Companion "$Out\$($t.case)\record.log" (@('-Record', '-GameDir', $g, '-Recording', $s.rec, '-Result', $s.res, '-RequestId', $id) + $common) 60
    Check ($r.code -eq 1 -and $r.text -match [regex]::Escape($t.want)) "$($t.case): refused ($($r.text.Trim()))"
    Check (-not (Get-CimInstance Win32_Process -Filter "Name='session_game_fixture.exe'" | Where-Object { $_.ExecutablePath -like "$g\*" })) "$($t.case): nothing launched"
    if ($t.stray) { Check ([System.IO.File]::ReadAllText("$fence\session-request.txt") -ceq 'someone else') 'stray-request: the other request is untouched'; Remove-Item "$fence\session-request.txt" }
    else { Check (Fence-Clean) "$($t.case): no request written" }
}

$left = @(Get-CimInstance Win32_Process -Filter "Name='session_game_fixture.exe'" | Where-Object { $_.ExecutablePath -like "$Out\*" })
foreach ($p in $left) { Stop-Process -Id $p.ProcessId -Force }   # this test's own fixtures only
Check ($left.Count -eq 0) 'no fixture left running'
if ($script:failed.Count) { "companion fixtures: $($script:failed.Count) of $($script:checks) checks FAILED"; exit 1 }
"companion fixtures: $($script:checks) checks passed"
