#Requires -Version 7.0
<# Private package entry. The existing helper owns game admission and all writes. #>
$ErrorActionPreference = 'Stop'

function Test-StandaloneHelper([string]$Folder) {
    $manifestSha = '635a8703f86384caea394b91a2ae8b05e369fd10234546af4d2d185996dba395'
    $folderItem = Get-Item -LiteralPath $Folder -Force
    for ($parent = $folderItem; $null -ne $parent; $parent = $parent.Parent) {
        if ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Helper reparse path refused.' }
    }
    $items = @(Get-ChildItem -LiteralPath $Folder -Recurse -Force)
    if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw 'Helper reparse payload refused.'
    }
    $files = @($items | Where-Object { -not $_.PSIsContainer })
    if ($files.Count -ne 528) { throw 'Helper incomplete or extra files present; restore the exact private package.' }
    $manifestPath = Join-Path $Folder 'bundle-manifest.json'
    if ((Get-Item -LiteralPath $manifestPath).Length -gt 1MB -or
        (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -ine $manifestSha) {
        throw 'Helper manifest hash mismatch; restore the exact private package.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.kind -cne 'private-standalone-helper' -or
        $manifest.sourceCommit -cne '6cb66313d9e0cd1e0cea41b49827feb992d73c2c' -or
        $manifest.rid -cne 'win-x64' -or $manifest.selfContained -ne $true -or
        @($manifest.files).Count -ne 527) { throw 'Unsupported helper manifest.' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [long]$total = 0
    foreach ($entry in $manifest.files) {
        $name = [string]$entry.path
        if ($name.Length -gt 240 -or -not $seen.Add($name) -or $name -ieq 'bundle-manifest.json') {
            throw 'Duplicate or invalid helper path.'
        }
        foreach ($part in $name.Split('/')) {
            if ($part -cnotmatch '^[A-Za-z0-9_][A-Za-z0-9_. -]*$' -or $part.EndsWith('.') -or
                $part.EndsWith(' ') -or $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') {
                throw 'Unsafe helper path.'
            }
        }
        $path = Join-Path $Folder $name
        $file = Get-Item -LiteralPath $path -Force
        $total += $file.Length
        if ($file.PSIsContainer -or $file.Length -gt 32MB -or $total -gt 256MB -or
            $file.Length -ne $entry.bytes -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $entry.sha256) {
            throw "Helper payload mismatch: $name; restore the exact private package."
        }
    }
    if (-not $seen.Contains('Start-ModSetup.ps1') -or -not $seen.Contains('DbceModSetup.exe')) {
        throw 'Helper launcher payload missing.'
    }
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($Folder, $file.FullName).Replace('\', '/')
        if ($relative -cne 'bundle-manifest.json' -and -not $seen.Contains($relative)) {
            throw "Unlisted helper payload: $relative"
        }
    }
}

try {
    $helper = Join-Path $PSScriptRoot 'Extras/StandaloneModSetup'
    Test-StandaloneHelper $helper
    # The qualified launcher/helper checks the closed game, capability and actual install receipt.
    & (Join-Path $helper 'Start-ModSetup.ps1') -Game 'outrun-2006-decomp' -GameDir $PSScriptRoot
    exit $LASTEXITCODE
} catch {
    Write-Error "Controls setup refused: $($_.Exception.Message)"
    exit 1
}
