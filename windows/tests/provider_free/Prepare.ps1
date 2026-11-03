param([Parameter(Mandatory)][string]$OwnerRoot,[Parameter(Mandatory)][string]$Candidate)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../../..").Path
$out=Join-Path $root 'build/provider-free'
$helpers=Join-Path $out 'helpers'
New-Item -ItemType Directory -Force $out,$helpers|Out-Null
function Checked([string]$Exe,[string[]]$Arguments){& $Exe @Arguments;if($LASTEXITCODE -ne 0){throw "$Exe failed: $LASTEXITCODE"}}
Checked cmake @('-S',$PSScriptRoot,'-B',"$out/native-clang",'-G','Visual Studio 17 2022','-A','x64','-T','ClangCL')
Checked cmake @('--build',"$out/native-clang",'--config','Release','--target','control_publish','capture_replay','--parallel','1')
Copy-Item "$out/native-clang/Release/*.exe" $helpers
Checked dotnet @('build',"$root/windows/tests/p21_recorder_integration/managed/P21Finalizer.csproj",'-c','Release','--artifacts-path',"$out/managed",'-o',$helpers,'-p:ToolkitRoot=E:/Source/toolkits/dbce-wheel-mod-toolkit','-m:1')
Checked py @('-3.14','-B',"$root/windows/tests/shim_first_loop/run.py",'--prepare-from',$OwnerRoot,'--game',"$out/game",'--candidate',$Candidate)
Write-Output "Prepared helpers and private game: $out"
