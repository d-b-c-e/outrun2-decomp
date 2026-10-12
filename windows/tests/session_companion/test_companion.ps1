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
                  [switch]$OlderRecord, [switch]$DuplicateRecord, [bool]$playbackEnabled = $true) {
    $g = "$Out\$case\game"; New-Item -ItemType Directory $g -Force | Out-Null
    Copy-Item $Fixture "$g\session_game_fixture.exe"
    [System.IO.File]::WriteAllText("$g\OutRunDesktop.ini", "[Input]`r`nBackend = $backend`r`n")
    # The owner's state a startup snapshot reads: a save in a subfolder and options.ini.
    New-Item -ItemType Directory "$g\SaveGame\profiles" -Force | Out-Null
    [System.IO.File]::WriteAllBytes("$g\SaveGame\license.sav", [byte[]](0x4C, 0x49, 0x43, 0, 1, 2))
    [System.IO.File]::WriteAllText("$g\SaveGame\profiles\best.bin", 'best')
    [System.IO.File]::WriteAllText("$g\options.ini", "[options]`r`nsound = 1`r`n")
    if ($Options) { [System.IO.File]::WriteAllText("$g\fixture-options.txt", $Options) }
    $fd = "$g\recording-finalizer"; New-Item -ItemType Directory $fd | Out-Null
    Get-ChildItem $Finalizer -File | Copy-Item -Destination $fd
    $map = [ordered]@{}; foreach ($f in Get-ChildItem $fd -File | Sort-Object Name) { $map[$f.Name] = Sha $f.FullName }
    if ($TamperFinalizer) { [System.IO.File]::AppendAllText("$fd\P21Finalizer.runtimeconfig.json", ' ') }
    $exeSha = Sha "$g\session_game_fixture.exe"
    $cap = [ordered]@{ schema = 'dbce.session-capabilities@1'; game = 'OutRun 2006 (decomp)'; protocol = 'outrun.session-request@1'; gameBuildSha256 = $exeSha
                       runner = 'OutRun-Session.ps1'; record = @{ enabled = $recordEnabled }
                       playback = [ordered]@{ enabled = $playbackEnabled; packageSchema = 'outrun.recording-package@2'; startupSchema = 'outrun.startup-state@1'
                                              comparisonSchema = 'outrun.consumed-comparison@1' } }
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
    Check ($d.schema -eq 'outrun.recording-package@2' -and $d.gameId -eq 'outrun-2006-decomp' -and $d.requestId -ceq $id -and $d.inputOrigin -eq 'automated' -and
           $d.recordingSealSha256 -ceq (Sha "$cap\recording-complete.tsv") -and $d.rows -gt 0 -and $d.durationUs -gt 0 -and $d.stoppedBy -eq 'stop-request' -and
           $d.gameBuildSha256 -ceq (Sha "$g\session_game_fixture.exe") -and $d.playback -eq $true -and
           $d.startupSealSha256 -ceq (Sha "$($s.rec)\startup\complete.tsv")) 'happy: the descriptor (recording-package@2: the startup seal, playable)'
    Check (@($d.PSObject.Properties.Name) -join ',' -ceq 'schema,gameId,requestId,recordedUtc,inputOrigin,recordingSealSha256,startupSealSha256,rows,durationUs,stoppedBy,gameBuildSha256,playback') 'happy: the descriptor has exactly the agreed fields'
    # The startup snapshot: the owner's saves, options and settings as they were, sealed (native startup_state.hpp).
    $seal = Get-Content "$($s.rec)\startup\complete.tsv"
    Check ($seal[0] -ceq 'outrun.startup-state@1' -and $seal[1] -ceq "requestId`t$id" -and $seal[2] -ceq "exeSha256`t$(Sha "$g\session_game_fixture.exe")") 'happy: the startup seal names the recording and its exe'
    Check ($seal -contains "file`tSaveGame/license.sav`t6`t$(Sha "$g\SaveGame\license.sav")" -and $seal -contains "file`tSaveGame/profiles/best.bin`t4`t$(Sha "$g\SaveGame\profiles\best.bin")" -and
           $seal -contains "file`toptions.ini`t$((Get-Item "$g\options.ini").Length)`t$(Sha "$g\options.ini")" -and
           $seal -contains "file`tOutRunDesktop.ini`t$((Get-Item "$g\OutRunDesktop.ini").Length)`t$(Sha "$g\OutRunDesktop.ini")") 'happy: the startup seal lists every member with its size and hash'
    Check ((Sha "$($s.rec)\startup\SaveGame\profiles\best.bin") -ceq (Sha "$g\SaveGame\profiles\best.bin") -and (Test-Path "$($s.res)\playback-admission.json")) 'happy: the members copied; the capture admitted for playback'
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
    # The companion admits on the game's own first status (its poll is 100 ms): let it see that before the forgery, or
    # it never admits (the game republishes only on a state change). A race in this test, seen once in 3 runs.
    Start-Sleep -Milliseconds 1000
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

# 7-11. Playback (Wheelkit docs/OUTRUN-PLAYBACK-CONTRACT.md): a recording made here, then played back from it.
function Record-Package([string]$case, [string]$g, [string]$options = '') {
    if ($options) { [System.IO.File]::WriteAllText("$g\fixture-options.txt", $options) } else { Remove-Item "$g\fixture-options.txt" -ErrorAction SilentlyContinue }
    $id = New-Id; $s = Session $case $id
    $c = Start-Companion "$Out\$case\record.log" (@('-Record', '-GameDir', $g, '-Recording', $s.rec, '-Result', $s.res, '-RequestId', $id) + $common)
    [void](Wait-Until { (Read-Json "$($s.res)\status.json").state -eq 'recording' } 30); Start-Sleep -Milliseconds 1500
    [void](Run-Companion "$Out\$case\stop.log" (@('-Stop', '-GameDir', $g, '-Result', $s.res, '-RequestId', $id) + $common))
    [void](Wait-Until { Test-Path "$($s.rec)\recording.json" } 60)
    [System.IO.File]::WriteAllText("$g\fixture-close.txt", '0'); [void](Finish-Companion $c 90); Remove-Item "$g\fixture-close.txt"
    return [pscustomobject]@{ s = $s; d = (Read-Json "$($s.rec)\recording.json") }
}
function Owner-Files([string]$g) { Get-ChildItem $g -Recurse -File | Where-Object { $_.Name -notlike 'fixture-*' } | ForEach-Object { "$($_.FullName.Substring($g.Length)) $(Sha $_.FullName)" } }
function Play([string]$case, [string]$g, $pkg, [string]$options = '', [string[]]$extra = @(), [switch]$NoWait) {
    if ($options) { [System.IO.File]::WriteAllText("$g\fixture-options.txt", $options) } else { Remove-Item "$g\fixture-options.txt" -ErrorAction SilentlyContinue }
    $id = New-Id; $res = "$Out\$case\sessions\playbacks\$id"
    $arguments = @('-Playback', '-GameDir', $g, '-Recording', $pkg.s.rec, '-Result', $res, '-RequestId', $id,
              '-RecordingSealSha256', $pkg.d.recordingSealSha256, '-StartupSealSha256', $pkg.d.startupSealSha256) + $extra + $common
    $c = Start-Companion "$Out\$case\playback-$id.log" $arguments
    if ($NoWait) { return [pscustomobject]@{ c = $c; id = $id; res = $res } }
    $r = Finish-Companion $c 180
    return [pscustomobject]@{ r = $r; id = $id; res = $res; j = (Read-Json "$res\result.json") }
}
& {
    $g = New-Game 'playback'; $pkg = Record-Package 'playback' $g
    Check ($pkg.d.playback -eq $true -and $pkg.d.rows -gt 30) "playback: a playable recording made ($($pkg.d.rows) rows)"
    $before = Owner-Files $g
    $p = Play 'playback' $g $pkg
    $j = $p.j; $cmp = Read-Json "$($p.res)\comparison.json"
    Check ($p.r.code -eq 0) "playback: the companion passes (exit $($p.r.code): $($p.r.text.Trim()))"
    Check ($j.schema -eq 'outrun.full-session-result@2' -and $j.action -eq 'playback' -and $j.requestId -ceq $p.id -and $j.recordingRequestId -ceq $pkg.d.requestId -and
           $j.recordingSealSha256 -ceq $pkg.d.recordingSealSha256 -and $j.startupSealSha256 -ceq $pkg.d.startupSealSha256 -and $j.scope -eq 'full-session' -and
           $j.state -eq 'complete' -and $j.passed -and $j.closed -and $j.exitCode -eq 0 -and $j.restored -and $j.ownerStateUnchanged) 'playback: result.json@2 complete, passed, closed normally, owner files unchanged'
    Check ($j.comparisonSha256 -ceq (Sha "$($p.res)\comparison.json") -and $cmp.valid -and $cmp.followed -and $cmp.playedRows -eq $pkg.d.rows -and $cmp.fedEqualRows -eq $pkg.d.rows -and
           $cmp.pose.compared -and $cmp.pose.pairedRows -gt 0 -and $cmp.referenceSealSha256 -ceq $pkg.d.recordingSealSha256) 'playback: comparison.json valid, followed, every row, the pose paired, bound to the recording'
    $proof = Read-Json "$($p.res)\replay\capture\recorder-complete.json"
    Check ($proof.action -eq 'playback' -and $proof.stoppedBy -eq 'tape-end' -and $proof.sourceSealSha256 -ceq $pkg.d.recordingSealSha256 -and
           (Test-Path "$($p.res)\replay\capture\recording-complete.tsv")) 'playback: the replay capture sealed: action playback, tape-end, the source seal'
    Check ((Test-Path "$($p.res)\home\SaveGame\license.sav") -and (Sha "$($p.res)\home\SaveGame\profiles\best.bin") -ceq (Sha "$g\SaveGame\profiles\best.bin")) 'playback: the private home seeded from the snapshot'
    Check ((Compare-Object @($before) @(Owner-Files $g)) -eq $null) 'playback: the owner''s game folder byte-identical (nothing added or changed)'
    Check ((Read-Json "$($p.res)\companion.json").action -eq 'playback' -and (Fence-Clean)) 'playback: companion.json names the action; nothing left in the request folder'

    # 8. The game diverges at one row: complete with valid evidence, not a pass (exit 2), the first divergence named.
    $p = Play 'playback' $g $pkg "diverge-at=5`r`n"
    $cmp = Read-Json "$($p.res)\comparison.json"
    Check ($p.r.code -eq 2 -and $p.j.state -eq 'complete' -and -not $p.j.passed -and $cmp.valid -and -not $cmp.followed -and $cmp.firstDivergence.row -eq 5 -and
           $cmp.firstDivergence.channel -eq 'gameInput.speed' -and $p.j.ownerStateUnchanged) "diverge: complete, not passed, row 5 gameInput.speed named (exit $($p.r.code))"

    # 9. A child that writes the owner's files: reported (never repaired), a failure.
    $p = Play 'playback' $g $pkg "owner-write`r`n"
    Check ($p.r.code -eq 1 -and $p.j.state -eq 'failed' -and -not $p.j.ownerStateUnchanged -and -not $p.j.restored -and -not $p.j.passed -and
           ($p.j.why -join ' ') -match 'options.ini') "owner-write: failed, owner files changed and named, not repaired (exit $($p.r.code))"
    Check ((Get-Content "$g\options.ini" -Raw) -match 'written during a playback') 'owner-write: the change is left as found (reported, not copied over)'
    [System.IO.File]::WriteAllText("$g\options.ini", "[options]`r`nsound = 1`r`n")

    # 10. A cancel (-Stop) during the playback: the child closed normally, a failure, never a pass.
    $p = Play 'playback' $g $pkg "tick-ms=150`r`n" -NoWait
    Check (Wait-Until { (Read-Json "$($p.res)\status.json").state -eq 'recording' } 30) 'cancel: playing'
    $stop = Run-Companion "$Out\playback\cancel.log" (@('-Stop', '-GameDir', $g, '-Result', $p.res, '-RequestId', $p.id) + $common)
    $r = Finish-Companion $p.c 120; $j = Read-Json "$($p.res)\result.json"
    Check ($stop.code -eq 0 -and $r.code -eq 1 -and $j.cancelled -and -not $j.passed -and $j.state -eq 'failed' -and $j.closed -and $j.exitCode -eq 0 -and $j.ownerStateUnchanged) "cancel: closed normally, failed, owner files unchanged (stop $($stop.code), exit $($r.code))"

    # 11. Refusals before anything is written or launched.
    $bad = Play 'playback' $g ([pscustomobject]@{ s = $pkg.s; d = [pscustomobject]@{ recordingSealSha256 = ('0' * 64); startupSealSha256 = $pkg.d.startupSealSha256 } })
    Check ($bad.r.code -eq 1 -and $bad.r.text -match 'not the one prepared' -and -not (Test-Path $bad.res)) "refused: another recording seal ($($bad.r.text.Trim()))"
    $still = Record-Package 'playback' $g "still`r`n"
    Check ($still.d.playback -eq $false -and $still.d.playbackWhy -match 'not admitted for playback') "a car that never moved: analysis only, with the reason ($($still.d.playbackWhy))"
    $bad = Play 'playback' $g ([pscustomobject]@{ s = $still.s; d = [pscustomobject]@{ recordingSealSha256 = $still.d.recordingSealSha256; startupSealSha256 = ('1' * 64) } })
    Check ($bad.r.code -eq 1 -and $bad.r.text -match 'not a playable package') "refused: an analysis-only recording ($($bad.r.text.Trim()))"
    $g2 = New-Game 'playback-disabled' -playbackEnabled $false; $pkg2 = Record-Package 'playback-disabled' $g2
    $bad = Play 'playback-disabled' $g2 $pkg2
    Check ($bad.r.code -eq 1 -and $bad.r.text -match 'playback is not an enabled capability') "refused: playback not enabled ($($bad.r.text.Trim()))"
    Check (Fence-Clean) 'playback refusals: no request written'
}

$left = @(Get-CimInstance Win32_Process -Filter "Name='session_game_fixture.exe'" | Where-Object { $_.ExecutablePath -like "$Out\*" })
foreach ($p in $left) { Stop-Process -Id $p.ProcessId -Force }   # this test's own fixtures only
Check ($left.Count -eq 0) 'no fixture left running'
if ($script:failed.Count) { "companion fixtures: $($script:failed.Count) of $($script:checks) checks FAILED"; exit 1 }
"companion fixtures: $($script:checks) checks passed"
