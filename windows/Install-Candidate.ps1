#Requires -Version 7.0
<#
.SYNOPSIS
Install or restore only OutRunDesktop.exe from a private candidate package.
.EXAMPLE
pwsh ./windows/Install-Candidate.ps1 -GameDir 'E:/Games/OutRun Decomp'
.EXAMPLE
pwsh ./windows/Install-Candidate.ps1 -Action Restore -GameDir 'E:/Games/OutRun Decomp' -ReceiptId '<id from install>'
.NOTES
No game launch, settings/save/assets/DLL edits or runtime acceptance. History is
retained under the destination. Restore refuses a newer/different executable.
#>
[CmdletBinding()]
param(
    [ValidateSet('Install','Restore')][string]$Action='Install',
    [Parameter(Mandatory)][string]$GameDir,
    [string]$PackageDir=(Split-Path -Parent $PSScriptRoot),
    [string]$ReceiptId
)
$ErrorActionPreference='Stop'
$exeName='OutRunDesktop.exe'
$historyName='.outrun-candidate-history'
function Assert-PlainPath([string]$Path) {
    $cursor=[IO.Path]::GetFullPath($Path)
    while($cursor) {
        if(Test-Path -LiteralPath $cursor) {
            $item=Get-Item -LiteralPath $cursor -Force
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse/symlink path refused: $cursor" }
        }
        $parent=[IO.Path]::GetDirectoryName($cursor)
        if($parent -eq $cursor) { break };$cursor=$parent
    }
}
function Get-Sha([string]$Path) {
    Assert-PlainPath $Path
    if(![IO.File]::Exists($Path)) { throw "Expected a regular file: $Path" }
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
function Write-Receipt([string]$Path,$Value) {
    $temporary=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $owned=$false
    try {
        $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$owned=$true
        try { $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 5)+"`n");$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true) } finally { $stream.Dispose() }
        [IO.File]::Move($temporary,$Path,$true);$owned=$false
    } finally { if($owned) { [IO.File]::Delete($temporary) } }
}
function Publish-Exe([string]$Source,[string]$ExpectedSource,[string]$ExpectedCurrent,[bool]$HadCurrent) {
    $stage=Join-Path $GameDir ('.OutRunDesktop.'+[Guid]::NewGuid().ToString('N')+'.tmp')
    $owned=$false
    try {
        $input=[IO.File]::OpenRead($Source)
        try {
            $output=[IO.File]::Open($stage,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$owned=$true
            try { $input.CopyTo($output);$output.Flush($true) } finally { $output.Dispose() }
        } finally { $input.Dispose() }
        if((Get-Sha $stage) -ne $ExpectedSource) { throw 'Staged executable hash differs; destination retained.' }
        if($HadCurrent) {
            if((Get-Sha $destination) -ne $ExpectedCurrent) { throw 'Destination changed during preparation; retained.' }
            [IO.File]::Replace($stage,$destination,[NullString]::Value)
        } else {
            [IO.File]::Move($stage,$destination) # no replacement if another file appeared
        }
        $owned=$false
        if((Get-Sha $destination) -ne $ExpectedSource) { throw 'Executable publication could not be verified. Retain the receipt and backup.' }
    } finally { if($owned) { [IO.File]::Delete($stage) } }
}
$GameDir=[IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($GameDir))
Assert-PlainPath $GameDir
if(![IO.Directory]::Exists($GameDir)) { throw 'GameDir must be an existing game folder.' }
$destination=Join-Path $GameDir $exeName
Assert-PlainPath $destination
if([IO.File]::Exists($destination) -and (Get-Item -LiteralPath $destination -Force).LinkType -eq 'HardLink') { throw 'Hard-linked destination refused; link ownership is retained.' }
foreach($process in @(Get-Process -Name OutRunDesktop -ErrorAction SilentlyContinue)) {
    if($process.Path -and [String]::Equals([IO.Path]::GetFullPath($process.Path),$destination,[StringComparison]::OrdinalIgnoreCase)) { throw 'Close this game before replacing or restoring its executable.' }
}
$history=Join-Path $GameDir $historyName
Assert-PlainPath $history
if($Action -eq 'Restore') {
    if($ReceiptId -notmatch '^\d{8}T\d{6}Z-[a-f0-9]{32}$') { throw 'Restore requires the exact ReceiptId returned by Install.' }
    $recordDir=Join-Path $history $ReceiptId
    $receiptPath=Join-Path $recordDir 'receipt.json'
    Assert-PlainPath $receiptPath
    $receipt=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if($receipt.schemaVersion -ne 1 -or $receipt.kind -ne 'outrun-exe-install' -or
        ![String]::Equals($receipt.gameDir,$GameDir,[StringComparison]::OrdinalIgnoreCase) -or
        $receipt.installedSha256 -notmatch '^[0-9a-fA-F]{64}$' -or $receipt.hadPrevious -isnot [bool] -or
        $receipt.state -notin @('Prepared','Installed','Restored')) { throw 'Receipt identity/state is invalid; no restore performed.' }
    $exists=[IO.File]::Exists($destination)
    $current=if($exists) { Get-Sha $destination } else { '' }
    $alreadyPrevious=if($receipt.hadPrevious) { $exists -and $current -eq $receipt.previousSha256 } else { !$exists }
    if($receipt.state -eq 'Restored') {
        if(!$alreadyPrevious) { throw 'Current executable differs from the restored state; retained.' }
        [pscustomobject]@{Status='AlreadyRestored';ReceiptId=$ReceiptId;Receipt=$receiptPath};return
    }
    if($receipt.state -eq 'Prepared' -and $alreadyPrevious) {
        $receipt.state='Restored';Write-Receipt $receiptPath $receipt
        [pscustomobject]@{Status='Restored';ReceiptId=$ReceiptId;Receipt=$receiptPath};return
    }
    if(!$exists -or $current -ne $receipt.installedSha256) { throw 'Current executable does not match this install receipt; newer/different bytes retained.' }
    if($receipt.hadPrevious) {
        $backup=Join-Path $recordDir 'previous.exe'
        if($receipt.previousSha256 -notmatch '^[0-9a-fA-F]{64}$' -or (Get-Sha $backup) -ne $receipt.previousSha256) { throw 'Previous executable backup hash differs; destination retained.' }
        Publish-Exe $backup $receipt.previousSha256 $current $true
    } else {
        [IO.File]::Delete($destination) # only this receipt's verified newly-created executable
    }
    $receipt.state='Restored';Write-Receipt $receiptPath $receipt
    [pscustomobject]@{Status='Restored';ReceiptId=$ReceiptId;Receipt=$receiptPath};return
}
$PackageDir=[IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($PackageDir))
Assert-PlainPath $PackageDir
if([String]::Equals($PackageDir,$GameDir,[StringComparison]::OrdinalIgnoreCase)) { throw 'PackageDir must be separate from GameDir.' }
$marker=$false
foreach($name in @('OR2006C2C.cache','OR2006C2C.exe')) {
    $path=Join-Path $GameDir $name;Assert-PlainPath $path
    if([IO.File]::Exists($path)) { $marker=$true }
}
if(!$marker) { throw 'GameDir needs an existing OR2006C2C.cache or OR2006C2C.exe. Supply your own game files.' }
$manifestPath=Join-Path $PackageDir 'candidate-manifest.json';Assert-PlainPath $manifestPath
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if($manifest.schemaVersion -ne 1 -or $manifest.kind -ne 'outrun-windows-private-alpha-candidate' -or
    $manifest.platform -ne 'windows-x64' -or $manifest.sourceCommit -notmatch '^[0-9a-f]{40}$') { throw 'Not a supported private Windows candidate manifest.' }
$members=@($manifest.members | Where-Object { $_.path -ieq $exeName })
if($members.Count -ne 1 -or $members[0].sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Manifest needs exactly one named executable hash.' }
$source=Join-Path $PackageDir $exeName
$sha=Get-Sha $source
$length=(Get-Item -LiteralPath $source).Length
if($length -lt 64 -or $length -gt 256MB -or $length -ne $members[0].bytes -or $sha -ne $members[0].sha256) { throw 'Candidate executable length/hash differs from its manifest.' }
$reader=[IO.BinaryReader]::new([IO.File]::OpenRead($source))
try {
    if($reader.ReadUInt16() -ne 0x5a4d) { throw 'Candidate is not a Windows executable.' }
    $reader.BaseStream.Position=0x3c;$pe=$reader.ReadInt32()
    if($pe -lt 64 -or $pe+26 -gt $length) { throw 'Invalid PE header.' }
    $reader.BaseStream.Position=$pe
    if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'Expected an AMD64 PE executable.' }
    $reader.BaseStream.Position=$pe+22;$flags=$reader.ReadUInt16();$magic=$reader.ReadUInt16()
    if(!($flags -band 2) -or ($flags -band 0x2000) -or $magic -ne 0x20b) { throw 'Expected a PE32+ executable, not a DLL.' }
} finally { $reader.Dispose() }
$hadPrevious=[IO.File]::Exists($destination)
$previous=if($hadPrevious) { Get-Sha $destination } else { '' }
if($hadPrevious -and $previous -eq $sha) { [pscustomobject]@{Status='AlreadyPresent';Sha256=$sha;ReceiptId=$null};return }
$ReceiptId=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')+'-'+[Guid]::NewGuid().ToString('N')
$recordDir=Join-Path $history $ReceiptId
if([IO.Directory]::Exists($recordDir) -or [IO.File]::Exists($recordDir)) { throw 'Receipt directory already exists; retained.' }
[IO.Directory]::CreateDirectory($recordDir) | Out-Null
Assert-PlainPath $recordDir
if($hadPrevious) {
    $backup=Join-Path $recordDir 'previous.exe';[IO.File]::Copy($destination,$backup,$false)
    if((Get-Sha $backup) -ne $previous) { throw 'Backup hash differs; destination retained.' }
}
$receiptPath=Join-Path $recordDir 'receipt.json'
$receipt=[ordered]@{schemaVersion=1;kind='outrun-exe-install';gameDir=$GameDir;sourceCommit=$manifest.sourceCommit;
    installedSha256=$sha;hadPrevious=$hadPrevious;previousSha256=$previous;state='Prepared';createdUtc=[DateTime]::UtcNow.ToString('O')}
Write-Receipt $receiptPath $receipt # also usable for recovery if interrupted after publication
Publish-Exe $source $sha $previous $hadPrevious
$receipt.state='Installed';Write-Receipt $receiptPath $receipt
[pscustomobject]@{Status='Installed';SourceCommit=$manifest.sourceCommit;Sha256=$sha;ReceiptId=$ReceiptId;Receipt=$receiptPath}
