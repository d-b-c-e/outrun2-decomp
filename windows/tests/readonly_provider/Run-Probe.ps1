#Requires -Version 7.0
# Caller owns the shared lease and checks owner idle before invoking this bounded, read-only device probe.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExecutableSha256,
    [Parameter(Mandatory)][string]$Profile,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ProfileSha256,
    [Parameter(Mandatory)][string]$Result,
    [Parameter(Mandatory)][string]$Toolkit,
    [Parameter(Mandatory)][string]$LeasePath,
    [Parameter(Mandatory)][string]$LeaseToken,
    [ValidateRange(1,4)][int]$ExpectedDevices=2
)
$ErrorActionPreference='Stop'
. (Join-Path $Toolkit 'tools/playback/Stage-RigLease.ps1')
Assert-StageRigLease -Path $LeasePath -Token $LeaseToken
$Result=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Result)
if(Test-Path -LiteralPath $Result){throw 'Result must be new.'}
if((Get-FileHash -LiteralPath $Executable).Hash -ine $ExecutableSha256 -or (Get-FileHash -LiteralPath $Profile).Hash -ine $ProfileSha256){throw 'Probe executable or profile differs from the reviewed inputs.'}
New-Item -ItemType Directory -Path $Result | Out-Null
$exe=Join-Path $Result 'readonly_provider.exe';$profileCopy=Join-Path $Result 'profile.ini'
[IO.File]::Copy((Resolve-Path -LiteralPath $Executable).Path,$exe,$false)
[IO.File]::Copy((Resolve-Path -LiteralPath $Profile).Path,$profileCopy,$false)
if((Get-FileHash -LiteralPath $exe).Hash -ine $ExecutableSha256 -or (Get-FileHash -LiteralPath $profileCopy).Hash -ine $ProfileSha256){throw 'Retained probe inputs differ.'}
$capture=Join-Path $Result 'observations'
$r=[ordered]@{schema='dbce.shim.readonly-provider@1';passed=$false;pid=$null;creationTime=$null;forced=$false;closed=$false;exitCode=$null;
    executableSha256=$ExecutableSha256;profileSha256=$ProfileSha256;lease=$LeaseToken;error=$null;cleanupErrors=@();scope='P29 only: real read-only provider; no recording, game, injection or force';host=$null}
$process=[Diagnostics.Process]::new();$started=$false
try {
    $process.StartInfo=[Diagnostics.ProcessStartInfo]::new($exe)
    $process.StartInfo.UseShellExecute=$false;$process.StartInfo.CreateNoWindow=$true
    $process.StartInfo.RedirectStandardOutput=$true;$process.StartInfo.RedirectStandardError=$true
    foreach($a in @('--read-only-device-probe',$profileCopy,$capture)){$process.StartInfo.ArgumentList.Add($a)}
    Assert-StageRigLease -Path $LeasePath -Token $LeaseToken
    $started=$process.Start();if(!$started){throw 'Probe did not start.'}
    $r.pid=$process.Id;$r.creationTime=$process.StartTime.ToUniversalTime().ToFileTimeUtc()
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Result 'receipt.json')
    if(!$process.WaitForExit(20000)){
        $r.forced=$true;$process.Kill()
        if(!$process.WaitForExit(5000)){throw 'Owned probe closure unresolved; caller must retain the lease.'}
        throw 'Probe exceeded its 20-second deadline; no runtime pass.'
    }
    $r.closed=$true;$r.exitCode=$process.ExitCode
    [IO.File]::WriteAllText((Join-Path $Result 'stdout.log'),$stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $Result 'stderr.log'),$stderr.GetAwaiter().GetResult())
    if($r.exitCode -ne 0){throw "Probe failed with exit $($r.exitCode)."}
    $h=Get-Content -Raw -LiteralPath (Join-Path $capture 'host-shutdown.json')|ConvertFrom-Json
    $r.host=$h
    if($h.pid -ne $r.pid -or $h.creationTime -ne $r.creationTime -or $h.state -cne 'resolved' -or $h.unresolved.Count -ne 0 -or
       !$h.inputReleased -or $h.released -ne $ExpectedDevices -or $h.held -ne 0){throw 'Host identity or normal shutdown proof differs.'}
    Assert-StageRigLease -Path $LeasePath -Token $LeaseToken
    if((Get-FileHash -LiteralPath $exe).Hash -ine $ExecutableSha256 -or (Get-FileHash -LiteralPath $profileCopy).Hash -ine $ProfileSha256){throw 'Retained inputs changed during the run.'}
    $r.passed=$true
} catch { $r.error=$_.Exception.Message; throw }
finally {
    try {
        if($started -and !$process.HasExited){
            $r.forced=$true;$process.Kill();$r.closed=$process.WaitForExit(5000)
        }elseif($started){$r.closed=$true}
        if($started -and $r.closed){$r.exitCode=$process.ExitCode}
        # Preserve output on nonzero exit and timeout too, after confirmed closure. A hung inherited pipe never
        # extends the deadline: unfinished tasks are recorded as a cleanup error rather than waited indefinitely.
        if($started -and $r.closed -and $stdout -and $stderr){
            if(!$stdout.IsCompleted -or !$stderr.IsCompleted){throw 'Process closed but its redirected output did not finish.'}
            [IO.File]::WriteAllText((Join-Path $Result 'stdout.log'),$stdout.GetAwaiter().GetResult())
            [IO.File]::WriteAllText((Join-Path $Result 'stderr.log'),$stderr.GetAwaiter().GetResult())
        }
    }catch{$r.cleanupErrors+= $_.Exception.Message;$r.passed=$false}
    try{$r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Result 'receipt.json')}
    finally{$process.Dispose()}
}
if(!$r.closed -or $r.cleanupErrors.Count){throw 'Owned probe closure unresolved; caller must retain the lease and inspect the receipt.'}
Write-Output ('PASS: read-only actual provider. Evidence: '+$Result)
