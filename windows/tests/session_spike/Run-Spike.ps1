param(
    [Parameter(Mandatory)][string]$ToolkitRoot,
    [Parameter(Mandatory)][string]$Result
)
$ErrorActionPreference = 'Stop'
$ToolkitRoot = (Resolve-Path -LiteralPath $ToolkitRoot).Path
$Result = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Result)
if (Test-Path -LiteralPath $Result) { throw 'Result must be a new directory; prior evidence is never overwritten.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
function Read-CleanPin([string]$Root, [string[]]$Scope) {
    $dirty = @(& git -C $Root status --porcelain --untracked-files=all -- @Scope)
    if ($LASTEXITCODE) { throw "Cannot inspect source status: $Root" }
    if ($dirty.Count) { throw "Commit the relevant source before running this audit: $Root`n$($dirty -join "`n")" }
    $commit = & git -C $Root rev-parse HEAD
    if ($LASTEXITCODE) { throw "Cannot pin source: $Root" }
    $paths = @(& git -C $Root ls-files -- @Scope)
    if ($LASTEXITCODE) { throw "Cannot enumerate source: $Root" }
    $files = @($paths | Sort-Object -Unique | ForEach-Object {
        [ordered]@{ path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $Root $_) -Algorithm SHA256).Hash }
    })
    return [ordered]@{ root=$Root; commit=$commit; clean=$true; files=$files }
}
# Pin every transitive local header/fixture/build input through the clean decomp tree.
$sourceScope = @('.')
$toolkitScope = @('native/session','dotnet/Dbce.Wheel.Recording','Directory.*','global.json','NuGet.Config')
$sourcePin = Read-CleanPin $repo $sourceScope
$toolkitPin = Read-CleanPin $ToolkitRoot $toolkitScope
$writer = Join-Path $ToolkitRoot 'native/session/session_writer.h'
$expectedWriter = 'A9DE5D49C55B9B4BE464FB173178D20A97972EACFF500AEE0267E30FB44D19DB'
if ((Get-FileHash -LiteralPath $writer -Algorithm SHA256).Hash -ne $expectedWriter) {
    throw 'The native writer differs from this audit pin; review the new writer before interpreting these counterexamples.'
}
[IO.Directory]::CreateDirectory($Result) | Out-Null
$before = [ordered]@{source=$sourcePin; toolkit=$toolkitPin}
$beforeJson = $before | ConvertTo-Json -Depth 8
$beforeJson | Set-Content -LiteralPath (Join-Path $Result 'source-pins.before.json') -Encoding utf8
$build = Join-Path $Result 'build/native'
$managedBuild = Join-Path $Result 'build/dotnet'
$capture = Join-Path $Result 'capture'
& cmake -S $PSScriptRoot -B $build "-DTOOLKIT_ROOT=$ToolkitRoot" -A x64
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
& cmake --build $build --config Release
if ($LASTEXITCODE) { throw 'C++ fixture build failed.' }
$exe = Join-Path $build 'Release/shim_session_spike.exe'
& $exe generate $capture
if ($LASTEXITCODE) { throw 'Native fixture generation failed.' }
& dotnet build (Join-Path $PSScriptRoot 'managed/Conformance.csproj') --configuration Release --artifacts-path $managedBuild "-p:ToolkitRoot=$ToolkitRoot"
if ($LASTEXITCODE) { throw 'Managed fixture build failed.' }
$managed = Join-Path $managedBuild 'bin/Conformance/release/Conformance.dll'
& dotnet $managed $capture
if ($LASTEXITCODE) { throw 'Managed wire audit failed.' }
& $exe replay $capture
if ($LASTEXITCODE) { throw 'Offline replay failed.' }
$after = [ordered]@{source=(Read-CleanPin $repo $sourceScope); toolkit=(Read-CleanPin $ToolkitRoot $toolkitScope)}
if (($after | ConvertTo-Json -Depth 8) -cne $beforeJson) { throw 'Source changed during the audit; no qualified receipt.' }
$pins = [ordered]@{
    qualification = 'offline-fixture-only'
    sourceCommit = $sourcePin.commit
    toolkitCommit = $toolkitPin.commit
    sourcePinsSha256 = (Get-FileHash -LiteralPath (Join-Path $Result 'source-pins.before.json')).Hash
    writerSha256 = (Get-FileHash -LiteralPath $writer -Algorithm SHA256).Hash
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    managedExecutableSha256 = (Get-FileHash -LiteralPath $managed -Algorithm SHA256).Hash
    sourceUnchanged = $true
    evidence = @()
}
foreach ($path in Get-ChildItem -LiteralPath $capture -File) {
    $pins.evidence += [ordered]@{ path=('capture/'+$path.Name); sha256=(Get-FileHash -LiteralPath $path.FullName).Hash; bytes=$path.Length }
}
$pins | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Result 'audit-receipt.json') -Encoding utf8
Write-Host "PASS: offline input/session contract evidence at $Result. No runtime recorder or game qualification."
