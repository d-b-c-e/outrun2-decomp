param([Parameter(Mandatory)][string]$ResultRoot)
$ErrorActionPreference='Stop'
if(Test-Path $ResultRoot){throw 'Fresh inert fixture folder required'}
$out=New-Item -ItemType Directory $ResultRoot
Add-Type -Path "$PSScriptRoot/Snapshot.cs"
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile("$PSScriptRoot/Invoke-Readback.ps1",[ref]$tokens,[ref]$errors)
$fn=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'State'},$true)
Invoke-Expression $fn.Extent.Text
$stateFile="$($out.FullName)/state.json";$observer=$false;$cleanup=$false
State $true
$si=[Diagnostics.ProcessStartInfo]::new('C:/Users/antho/AppData/Local/Programs/Python/Python314/python.exe')
$si.UseShellExecute=$false;$si.CreateNoWindow=$true
foreach($a in @('-B',"$PSScriptRoot/snapshot_reader.py",$stateFile,"$($out.FullName)/reader.json")){$si.ArgumentList.Add($a)}
$child=[Diagnostics.Process]::Start($si);$writeErrors=@()
for($i=0;$i -lt 1000;$i++){try{State $true}catch{$writeErrors+=$_.Exception.Message}}
if(-not $child.WaitForExit(5000)){$child.Kill();throw 'Own inert reader timeout'}
$read=Get-Content "$($out.FullName)/reader.json" -Raw|ConvertFrom-Json
$pass=$child.ExitCode -eq 0 -and $writeErrors.Count -eq 0 -and @($read.errors.psobject.Properties).Count -eq 0 -and $read.reads -gt 100
@{pass=$pass;writes=1000;writeErrors=$writeErrors;reader=$read}|ConvertTo-Json -Depth 5|Set-Content "$($out.FullName)/result.json"
@{pass=$pass;writes=1000;writeErrors=$writeErrors.Count;reads=$read.reads;readErrorKinds=@($read.errors.psobject.Properties).Count}|ConvertTo-Json -Compress
if(-not $pass){throw 'Snapshot sharing integration failed'}
