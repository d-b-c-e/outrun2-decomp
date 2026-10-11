param(
    [Parameter(Mandatory)][string]$ToolkitRoot,
    [Parameter(Mandatory)][string]$Result
)
$ErrorActionPreference = 'Stop'
$ToolkitRoot = (Resolve-Path -LiteralPath $ToolkitRoot).Path
$Result = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Result)
if (Test-Path -LiteralPath $Result) { throw 'Result must be a new directory; prior evidence is never overwritten.' }
$writer = Join-Path $ToolkitRoot 'native/session/session_writer.h'
$expectedWriter = 'A9DE5D49C55B9B4BE464FB173178D20A97972EACFF500AEE0267E30FB44D19DB'
if ((Get-FileHash -LiteralPath $writer -Algorithm SHA256).Hash -ne $expectedWriter) {
    throw 'The native writer differs from this audit pin; review the new writer before interpreting these counterexamples.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$build = Join-Path $repo 'build-session-spike'
& cmake -S $PSScriptRoot -B $build "-DTOOLKIT_ROOT=$ToolkitRoot" -A x64
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
& cmake --build $build --config Release
if ($LASTEXITCODE) { throw 'C++ fixture build failed.' }
$exe = Join-Path $build 'Release/shim_session_spike.exe'
& $exe generate $Result
if ($LASTEXITCODE) { throw 'Native fixture generation failed.' }
& dotnet run --project (Join-Path $PSScriptRoot 'managed/Conformance.csproj') "-p:ToolkitRoot=$ToolkitRoot" -- $Result
if ($LASTEXITCODE) { throw 'Managed wire audit failed.' }
& $exe replay $Result
if ($LASTEXITCODE) { throw 'Offline replay failed.' }
$pins = [ordered]@{
    qualification = 'offline-fixture-only'
    sourceCommit = (& git -C $repo rev-parse HEAD)
    toolkitCommit = (& git -C $ToolkitRoot rev-parse HEAD)
    writerSha256 = (Get-FileHash -LiteralPath $writer -Algorithm SHA256).Hash
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    sourceFiles = @()
    evidence = @()
}
foreach ($path in @('spike.cpp','CMakeLists.txt','Run-Spike.ps1','managed/Program.cs','managed/Conformance.csproj')) {
    $pins.sourceFiles += [ordered]@{ path=$path; sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $path)).Hash }
}
foreach ($path in Get-ChildItem -LiteralPath $Result -File) {
    $pins.evidence += [ordered]@{ path=$path.Name; sha256=(Get-FileHash -LiteralPath $path.FullName).Hash; bytes=$path.Length }
}
$pins | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Result 'audit-receipt.json') -Encoding utf8
Write-Host "PASS: offline input/session contract evidence at $Result. No runtime recorder or game qualification."
