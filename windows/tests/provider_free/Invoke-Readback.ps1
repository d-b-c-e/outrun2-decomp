param([ValidateSet('Check','Live')][string]$Mode='Check',[string]$Go,[string]$PreparedSha256,
 [string]$OwnerRoot='E:/Source/toolkits/launchbox/Launchbox-Racing/Games/Windows/Outrun 2006 Coast to Coast/OutRun2006 Coast 2 Coast (Decomp)')
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../../..").Path
$out=Join-Path $root 'build/provider-free'
$game=Join-Path $out 'game';$result=Join-Path $out 'r1'
$gameExe=(Get-Item "$game/OutRunDesktop.exe").FullName
$python='C:/Users/antho/AppData/Local/Programs/Python/Python314/python.exe'
$jobDll='E:/Source/_work/soba-truckful-provider-free/artifacts/provider-free/rehearsal-native-go501049-amend501065/native/Truckful.Live.dll'
$observerDll='E:/Source/_work/remi-dirt4-loose-map-20261103/build/ReadbackObserver.dll'
function Pin([string]$Path,[string]$Hash){if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -cne $Hash){throw "Pin mismatch: $Path"}}
Pin "$game/OutRunDesktop.exe" 'D36203770FCEF774DD67F839410A67468349D7F0148DB8AC12BD371503870B7D'
Pin $jobDll '725E52B00A36B8189BF4BAF3CF201D1266614B49DB124F2C410884D2D96BE4DD'
Pin $observerDll '2E45A82CD860C5A819D046746C5B1787B4B3939C8D35180498708C161D9BA85D'
$argsRun=@('-B',"$root/windows/tests/shim_first_loop/run.py",'--lease-held','--game',$game,'--candidate',$gameExe,'--result',$result,'--publisher-tool',"$out/helpers/control_publish.exe",'--finalizer',"$out/helpers/P21Finalizer.dll",'--replay-tool',"$out/helpers/capture_replay.exe",'--seconds','90','--drive','30','--render','screens=separate,views=angled,frames=12,every=6,after=120,scale=2','--retain-on-close-timeout')
$oldAppData=$env:LOCALAPPDATA;$oldMute=$env:DBCE_FFB_MUTE
if($Mode -eq 'Check'){
 try{$env:LOCALAPPDATA="$out/appdata";& $python @argsRun --check-only;if($LASTEXITCODE){throw 'harness preflight failed'}}finally{$env:LOCALAPPDATA=$oldAppData}
 return
}
if(-not $Go){throw 'Live requires a concrete coordinator GO id'}
if(-not $PreparedSha256){throw 'Live requires the prepared manifest SHA approved in that GO'}
Pin "$out/prepared.json" $PreparedSha256
$prepared=Get-Content "$out/prepared.json" -Raw|ConvertFrom-Json
if([IO.Path]::GetFullPath($OwnerRoot) -ine $prepared.ownerRoot){throw 'Owner inventory root differs from approved preparation'}
foreach($p in $prepared.pins){Pin $p.path $p.sha256}
if(Test-Path "$out/live"){throw 'Fresh live evidence directory required; never overwrite an attempt'}
$live=New-Item -ItemType Directory "$out/live"
$stateFile="$live/supervisor.json";$lease=$false;$observer=$false;$cleanup=$false;$launched=$false;$adopted=$false;$quiescent=$true;$restored=$false;$failure=$null
function State([bool]$Ok,[bool]$Fence=$false){
 $s=@{unix=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0;ok=$Ok;pending=($observer -and [Dirt4Readback.Observer]::Pending);fenceReady=$Fence;cleanup=$cleanup}
 $s|ConvertTo-Json -Compress|Set-Content "$stateFile.tmp" -Encoding utf8
 [OutRunSnapshot]::Publish("$stateFile.tmp",$stateFile)
}
function Inventory([string]$Dest){& $python -B "$PSScriptRoot/inventory.py" $OwnerRoot $Dest;if($LASTEXITCODE){throw 'Owner inventory failed'}}
function ExactGameWindows {
 if(-not $script:identity){return @()}
 $p=Get-Process -Id $script:identity.pid -ErrorAction SilentlyContinue
 if($p -and $p.StartTime.ToUniversalTime().Ticks -eq $script:identity.birth -and $p.MainWindowHandle -ne 0){return @($p.MainWindowHandle)}
 return @()
}
try {
 Add-Type -Path $jobDll;Add-Type -Path $observerDll;Add-Type -Path "$PSScriptRoot/Snapshot.cs"
 if([Dirt4Readback.Observer]::RawIdleSeconds -lt 180){throw 'Fresh raw idle <180 seconds'}
 & E:/Source/dbce-project-mgmt/tools/Rig-Lease.ps1 -Enter -Name remi-outrun-live -Purpose "GO$Go standalone frozen9a10ace angled R1" -IdleMinutes 3
 if(-not $?){throw 'Lease refused'};$lease=$true
 Inventory "$live/owner-before.json"
 $display=[TruckfulLive]::DisplayIdentity();$display|Set-Content "$live/display-before.txt"
 if([Dirt4Readback.Observer]::RawIdleSeconds -lt 180){throw 'Owner activity during snapshot'}
 [Dirt4Readback.Observer]::Start($live.FullName);$observer=$true
 [Dirt4Readback.Observer]::AssertHealthy()
 [Dirt4Readback.Observer]::ExpectGame($gameExe,[DateTime]::UtcNow)
 State $true
 # Only this process's child environment is redirected. Lease/token paths continue using real AppData.
 $env:LOCALAPPDATA="$out/appdata";$env:DBCE_FFB_MUTE='1'
 try{
  $all=@('-B',"$PSScriptRoot/bridge.py",$argsRun[1],"$live/runner.txt")+$argsRun[2..($argsRun.Count-1)]+@('--supervisor-state',$stateFile)
  foreach($a in $all){if($a.Contains('"')){throw 'Unexpected quote in child argument'}}
  $rootIdentity=[TruckfulLive]::Launch($python,$root,(($all|ForEach-Object{'"'+$_+'"'}) -join ' '))
  $launched=$true;$quiescent=$false
 }finally{$env:LOCALAPPDATA=$oldAppData;$env:DBCE_FFB_MUTE=$oldMute}
 $rootIdentity|ConvertTo-Json|Set-Content "$live/root.json"
 $clock=[Diagnostics.Stopwatch]::StartNew()
 while(@([TruckfulLive]::JobPids()).Count){
  if(Test-Path "$live/supervisor.closing"){
   if(-not $cleanup){[Dirt4Readback.Observer]::BeginCleanup();$cleanup=$true}
  }
  if(-not $cleanup){[Dirt4Readback.Observer]::AssertHealthy()}
  if([TruckfulLive]::DisplayIdentity() -cne $display){throw 'Display topology/mode changed'}
  $ids=@([TruckfulLive]::JobPids())
  $games=@(foreach($childId in $ids){try{$i=[TruckfulLive]::ProcessIdentity($childId);if($i.image -ieq $gameExe){$i}}catch{if(Get-Process -Id $childId -ErrorAction SilentlyContinue){throw}}})
  if($games.Count -gt 1){throw 'More than one game child'}
  if($games.Count -and -not $adopted){$script:identity=$games[0];[Dirt4Readback.Observer]::Adopt($identity.pid,$identity.birth);$adopted=$true;$identity|ConvertTo-Json|Set-Content "$live/game.json"}
  if($games.Count -and $adopted -and ($games[0].pid -ne $identity.pid -or $games[0].birth -ne $identity.birth)){throw 'Game identity changed'}
  $logs=@(Get-ChildItem "$out/appdata/dbce/outrun2006-decomp/run-*/runtime.log" -ErrorAction SilentlyContinue)
  if($logs.Count -gt 1){throw 'Ambiguous run log'}
  $fence=$false
  if($logs.Count){$text=Get-Content $logs[0].FullName -Raw;$fence=($text -match 'TEST PROCESS: inject.on was present at start. Wheel input is off' -and $text -match 'ffb: [^\r\n]*output none')
   if($text -match 'ffb: wheel output to|output WheelFfb|passing-to-Delivery'){throw 'Unexpected physical output admission'}
  }
  State $true $fence
  if($clock.Elapsed.TotalSeconds -gt 240){throw 'Total own-job deadline exceeded'}
  Start-Sleep -Milliseconds 100
 }
 $quiescent=$true
 if(-not $adopted){throw 'No exact owned game adopted'}
 $receipt=Get-Content "$result/receipt.json" -Raw|ConvertFrom-Json
 if($receipt.result -ne 'pass' -or $receipt.forced -or $receipt.render.frames -ne 12){throw 'R1 receipt/frame assertion failed'}
} catch {$failure=$_.Exception.Message}
finally {
 $env:LOCALAPPDATA=$oldAppData;$env:DBCE_FFB_MUTE=$oldMute
 if($lease){
  State $false
  if($observer){[Dirt4Readback.Observer]::BeginCleanup();$cleanup=$true;State $false}
  if($launched -and -not $quiescent){
   foreach($h in (ExactGameWindows)){[void][TruckfulLive]::PostMessage($h,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
   $closing=[Diagnostics.Stopwatch]::StartNew()
   while(@([TruckfulLive]::JobPids()).Count -and $closing.Elapsed.TotalSeconds -lt 45){Start-Sleep -Milliseconds 100}
   $quiescent=@([TruckfulLive]::JobPids()).Count -eq 0
  }
  if($quiescent){
   try{
    Inventory "$live/owner-after.json"
    if((Get-FileHash "$live/owner-before.json").Hash -cne (Get-FileHash "$live/owner-after.json").Hash){throw 'Owner SHA/UTCticks/attributes inventory drift'}
    if([TruckfulLive]::DisplayIdentity() -cne $display){throw 'Display identity drift'}
    $commands=@(Get-ChildItem "$out/appdata/dbce/outrun2006-decomp" -File -ErrorAction SilentlyContinue|Where-Object Name -in @('inject.on','inject.txt','render-test.txt'))
    if($commands.Count){throw 'Private control files retained'}
    $restored=$true
   }catch{$failure="${failure}; verification: $($_.Exception.Message)"}
  }
  if($observer){try{if(-not [Dirt4Readback.Observer]::Stop()){throw 'LL stop failed'};$observer=$false}catch{$failure="${failure}; observer: $($_.Exception.Message)"}}
  if($quiescent -and $restored -and -not $observer){[TruckfulLive]::ReleaseJob();& E:/Source/dbce-project-mgmt/tools/Rig-Lease.ps1 -Exit -Name remi-outrun-live;$lease=$false}
  @{go=$Go;failure=$failure;quiescent=$quiescent;ownerVerified=$restored;leaseHeld=$lease;observerHeld=$observer}|ConvertTo-Json|Set-Content "$live/result.json"
  if($lease){Write-Output 'CUSTODY_RETAINED: controller, job handle and lease remain alive; explicit resolution required';while($true){Start-Sleep -Seconds 1}}
 }
}
if($failure){throw $failure}
Get-Content "$live/result.json"
