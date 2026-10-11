#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Result,[Parameter(Mandatory)][string]$Toolkit)
$ErrorActionPreference='Stop'
$Result=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Result)
if(Test-Path -LiteralPath $Result){throw 'Use a fresh fixture result.'}
New-Item -ItemType Directory -Path $Result|Out-Null
$supervisor=Join-Path $PSScriptRoot 'Run-Probe.ps1'
$before=(Get-FileHash $supervisor).Hash
& dotnet publish (Join-Path $PSScriptRoot 'supervisor_fixture/Child.csproj') -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true --artifacts-path "$Result/build" -o "$Result/publish" --verbosity quiet
if($LASTEXITCODE){throw 'Fake child did not build.'}
. (Join-Path $Toolkit 'tools/playback/Stage-RigLease.ps1')
# Disjoint dummy lease; the fake child has no device/game code and does not reserve the actual rig.
$lease=Enter-StageRigLease -Path "$Result/fixture-lease.txt" -Owner 'supervisor-fixture' -Purpose 'Fake-child process regression, no devices'
$checks=0
function Check([bool]$ok,[string]$why){$script:checks++;if(!$ok){throw "FAIL: $why"}}
$rows=[Collections.Generic.List[object]]::new()
try{
    $exe="$Result/publish/Child.exe"
    $cases=@('normal','exit','pid','creation','unresolved','input','held','hang'|ForEach-Object {@{name=$_;mode=$_;expected=2;pass=($_ -eq 'normal')}})
    $cases+=@{name='extra-device';mode='three';expected=2;pass=$false},@{name='three-devices';mode='three';expected=3;pass=$true},@{name='missing-device';mode='normal';expected=3;pass=$false}
    foreach($case in $cases){
        $mode=$case.mode;$name=$case.name
        $profile="$Result/$name.txt";[IO.File]::WriteAllText($profile,$mode)
        $out="$Result/result-$name";$errorText=$null;$watch=[Diagnostics.Stopwatch]::StartNew()
        try{& $supervisor -Executable $exe -ExecutableSha256 (Get-FileHash $exe).Hash -Profile $profile -ProfileSha256 (Get-FileHash $profile).Hash -Result $out -Toolkit $Toolkit -LeasePath $lease.path -LeaseToken $lease.token -ExpectedDevices $case.expected}catch{$errorText=$_.Exception.Message}
        $r=Get-Content "$out/receipt.json" -Raw|ConvertFrom-Json
        Check ($r.closed -eq $true) "$name child closed"
        Check ($r.passed -eq $case.pass) "$name verdict"
        Check (($null -ne $errorText) -ne $case.pass) "$name threw only on refusal"
        Check ($r.forced -eq ($mode -eq 'hang')) "$name force classification"
        Check ($watch.Elapsed.TotalSeconds -lt 30) "$name bounded"
        Assert-StageRigLease -Path $lease.path -Token $lease.token
        Check (!(Get-Process -Id $r.pid -ErrorAction SilentlyContinue)) "$name no child left"
        Check ((Get-Content "$out/stdout.log" -Raw).Contains("fixture child started: $mode")) "$name stdout retained"
        Check ((Get-Content "$out/stderr.log" -Raw).Contains('fixture stderr retained')) "$name stderr retained"
        $rows.Add(@{case=$case;error=$errorText;receipt=$r})
    }
    Check ((Get-FileHash $supervisor).Hash -ceq $before) 'supervisor source unchanged'
    @{checks=$checks;cases=$rows;supervisorSha256=$before;scope='fake child only; dummy lease, no game or devices'}|ConvertTo-Json -Depth 12|Set-Content "$Result/fixture-receipt.json"
    "PASS $checks fake-child supervisor checks"
}finally{if(!(Exit-StageRigLease $lease)){throw 'Dummy lease release failed.'}}
