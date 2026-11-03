# Run after offline checks and a local source commit. No game, hook, lease or owner write.
param([Parameter(Mandatory)][ValidatePattern('^attempt-[0-9]{2}$')][string]$Attempt,
 [string]$OwnerRoot='E:/Source/toolkits/launchbox/Launchbox-Racing/Games/Windows/Outrun 2006 Coast to Coast/OutRun2006 Coast 2 Coast (Decomp)')
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../../..").Path;$assets=Join-Path $root 'build/provider-free';$out=Join-Path $assets $Attempt
if(Test-Path "$out/prepared.json"){throw 'Immutable manifest already exists; use a fresh attempt'}
New-Item -ItemType Directory -Force $out|Out-Null
$files=@(Get-ChildItem $PSScriptRoot -File|Where-Object Name -notlike '*.md')
$files+=Get-Item "$root/windows/tests/shim_first_loop/run.py","$root/windows/tests/shim_first_loop/compare_replay.py","$root/windows/tests/shim_first_loop/compare_consumed.py","$root/windows/tests/control_journal/read_journal.py"
$files+=Get-ChildItem "$assets/helpers" -File
$files+=Get-Item "$assets/game/OutRunDesktop.exe","$assets/game/OutRunDesktop.ini",'C:/Users/antho/AppData/Local/Programs/Python/Python314/python.exe','E:/Source/_work/soba-truckful-provider-free/artifacts/provider-free/rehearsal-native-go501049-amend501065/native/Truckful.Live.dll','E:/Source/_work/remi-dirt4-loose-map-20261103/build/ReadbackObserver.dll'
$pins=@($files|Sort-Object FullName -Unique|ForEach-Object{@{path=$_.FullName;sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}})
$commit=git -C $root rev-parse HEAD
@{sourceCommit=$commit;candidateSource='9a10acef8b5adb4f43cf77719ef19249918fb96e';attemptRoot=[IO.Path]::GetFullPath($out);ownerRoot=[IO.Path]::GetFullPath($OwnerRoot);pins=$pins;scope='standalone Separate/Angled latched R1; 90s startup,30s drive,12 game-owned frames; no developer curve/sun gate';createdUtc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 5|Set-Content "$out/prepared.json" -Encoding utf8
Get-FileHash "$out/prepared.json" -Algorithm SHA256
