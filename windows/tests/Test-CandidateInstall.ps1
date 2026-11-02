#Requires -Version 7.0
# Synthetic file transactions only. Never launches the fake EXE or a game.
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$installer=Join-Path $repo 'windows/Install-Candidate.ps1'
$parent=Join-Path $repo 'work/validation-20261102/install-fixtures'
$token=[Guid]::NewGuid().ToString('N');$root=Join-Path $parent $token
$checks=0;$passed=$false
function Check([bool]$Value,[string]$Why) { $script:checks++;if(!$Value) { throw "Installer fixture: $Why" } }
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path).Hash }
function Store([string]$Path,[byte[]]$Bytes) { [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))|Out-Null;[IO.File]::WriteAllBytes($Path,$Bytes) }
function Refuses([scriptblock]$Run,[string]$Pattern) {
    $refused=$false
    try { & $Run|Out-Null } catch { if($_.Exception.Message -notmatch $Pattern) { throw };$refused=$true }
    Check $refused "expected refusal matching $Pattern"
}
function Fake-Exe([byte]$Tag) {
    $bytes=[byte[]]::new(512);$bytes[0]=0x4d;$bytes[1]=0x5a
    [BitConverter]::GetBytes([int]64).CopyTo($bytes,0x3c);[BitConverter]::GetBytes([uint32]0x4550).CopyTo($bytes,64)
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($bytes,68);[BitConverter]::GetBytes([uint16]1).CopyTo($bytes,70)
    [BitConverter]::GetBytes([uint16]240).CopyTo($bytes,84);[BitConverter]::GetBytes([uint16]0x22).CopyTo($bytes,86)
    [BitConverter]::GetBytes([uint16]0x20b).CopyTo($bytes,88);$bytes[511]=$Tag;return $bytes
}
try {
    Check (![IO.Directory]::Exists($root)) 'unique fixture root'
    [IO.Directory]::CreateDirectory($root)|Out-Null;[IO.File]::WriteAllText((Join-Path $root 'owned.txt'),$token)
    $package=Join-Path $root 'package [private]';$game=Join-Path $root 'game [synthetic]'
    $candidate=Join-Path $package 'OutRunDesktop.exe';Store $candidate (Fake-Exe 2)
    $candidateSha=Hash $candidate
    $manifest=@{schemaVersion=1;kind='outrun-windows-private-alpha-candidate';platform='windows-x64';sourceCommit=('a'*40);
        members=@(@{path='OutRunDesktop.exe';bytes=512;sha256=$candidateSha})}
    [IO.File]::WriteAllText((Join-Path $package 'candidate-manifest.json'),($manifest|ConvertTo-Json -Depth 4))
    Store (Join-Path $game 'OR2006C2C.cache') ([byte[]]@(1,2,3)) # marker only; no retail data
    $exe=Join-Path $game 'OutRunDesktop.exe';Store $exe (Fake-Exe 1);$oldSha=Hash $exe
    $kept=@{}
    foreach($name in @('OutRunDesktop.ini','options.ini','WheelFfb.dll','OR2006C2C.cache','SaveGame/License1.dat','textures/owner.dds')) {
        $path=Join-Path $game $name;Store $path ([Text.Encoding]::UTF8.GetBytes("owner sentinel $name`r`nunknown=kept"));$kept[$name]=Hash $path
    }
    $installed=& $installer -GameDir $game -PackageDir $package
    Check ($installed.Status -eq 'Installed' -and (Hash $exe) -eq $candidateSha) 'verified install'
    $recordDir=Join-Path (Join-Path $game '.outrun-candidate-history') $installed.ReceiptId
    Check ((Hash (Join-Path $recordDir 'previous.exe')) -eq $oldSha) 'exact prior executable backup'
    foreach($name in $kept.Keys) { Check ((Hash (Join-Path $game $name)) -eq $kept[$name]) "preserved $name" }
    $noOp=& $installer -GameDir $game -PackageDir $package;Check ($noOp.Status -eq 'AlreadyPresent' -and !$noOp.ReceiptId) 'matching candidate is a no-op'
    $restored=& $installer -Action Restore -GameDir $game -ReceiptId $installed.ReceiptId
    Check ($restored.Status -eq 'Restored' -and (Hash $exe) -eq $oldSha) 'exact restore'
    Check ((& $installer -Action Restore -GameDir $game -ReceiptId $installed.ReceiptId).Status -eq 'AlreadyRestored') 'idempotent restore'
    foreach($name in $kept.Keys) { Check ((Hash (Join-Path $game $name)) -eq $kept[$name]) "restore preserves $name" }
    # Simulate interruption after restore publication, before Installed ->
    # Restored receipt publication. A retry must recognize exact prior bytes.
    $receipt=Get-Content -LiteralPath $installed.Receipt -Raw|ConvertFrom-Json;$receipt.state='Installed'
    [IO.File]::WriteAllText($installed.Receipt,($receipt|ConvertTo-Json))
    $interruptedRestore=& $installer -Action Restore -GameDir $game -ReceiptId $installed.ReceiptId
    Check ($interruptedRestore.Status -eq 'Restored' -and (Hash $exe) -eq $oldSha) 'interrupted replacement restore can finish receipt'
    Check ((Get-Content -LiteralPath $installed.Receipt -Raw|ConvertFrom-Json).state -eq 'Restored') 'replacement recovery receipt is durable'
    Check ((& $installer -Action Restore -GameDir $game -ReceiptId $installed.ReceiptId).Status -eq 'AlreadyRestored') 'replacement recovery is idempotent'
    $installed=& $installer -GameDir $game -PackageDir $package
    Store $exe (Fake-Exe 3);$newerSha=Hash $exe
    Refuses { & $installer -Action Restore -GameDir $game -ReceiptId $installed.ReceiptId } 'newer/different'
    Check ((Hash $exe) -eq $newerSha) 'newer owner executable retained'
    Store $exe (Fake-Exe 2)
    $recordDir=Join-Path (Join-Path $game '.outrun-candidate-history') $installed.ReceiptId
    Store (Join-Path $recordDir 'previous.exe') ([byte[]]@(9,9,9))
    Refuses { & $installer -Action Restore -GameDir $game -ReceiptId $installed.ReceiptId } 'backup hash differs'
    Check ((Hash $exe) -eq $candidateSha) 'bad backup cannot replace candidate'
    Store $candidate (Fake-Exe 4)
    Refuses { & $installer -GameDir $game -PackageDir $package } 'length/hash differs'
    Check ((Hash $exe) -eq $candidateSha) 'tampered package cannot replace destination';Store $candidate (Fake-Exe 2)
    Refuses { & $installer -Action Restore -GameDir $game -ReceiptId '../outside' } 'exact ReceiptId'
    $cold=Join-Path $root 'new native install';Store (Join-Path $cold 'OR2006C2C.exe') ([byte[]]@(1))
    $first=& $installer -GameDir $cold -PackageDir $package
    Check ([IO.File]::Exists((Join-Path $cold 'OutRunDesktop.exe'))) 'new native executable created'
    $null=& $installer -Action Restore -GameDir $cold -ReceiptId $first.ReceiptId
    Check (![IO.File]::Exists((Join-Path $cold 'OutRunDesktop.exe')) -and [IO.File]::Exists((Join-Path $cold 'OR2006C2C.exe'))) 'new-file rollback removes only owned executable'
    $receipt=Get-Content -LiteralPath $first.Receipt -Raw|ConvertFrom-Json;$receipt.state='Installed'
    [IO.File]::WriteAllText($first.Receipt,($receipt|ConvertTo-Json))
    Check ((& $installer -Action Restore -GameDir $cold -ReceiptId $first.ReceiptId).Status -eq 'Restored') 'interrupted new-file restore can finish receipt'
    Check (![IO.File]::Exists((Join-Path $cold 'OutRunDesktop.exe')) -and (Get-Content -LiteralPath $first.Receipt -Raw|ConvertFrom-Json).state -eq 'Restored') 'new-file recovery preserves absence and receipt'
    Check ((& $installer -Action Restore -GameDir $cold -ReceiptId $first.ReceiptId).Status -eq 'AlreadyRestored') 'new-file recovery is idempotent'
    $empty=Join-Path $root 'not a game';[IO.Directory]::CreateDirectory($empty)|Out-Null
    Refuses { & $installer -GameDir $empty -PackageDir $package } 'existing OR2006C2C'
    Check ((Get-ChildItem -LiteralPath $empty -Force).Count -eq 0) 'refused target stays empty'
    # A Prepared receipt recovers both sides of the atomic publication boundary.
    $prepared=& $installer -GameDir $cold -PackageDir $package
    $receipt=Get-Content -LiteralPath $prepared.Receipt -Raw|ConvertFrom-Json;$receipt.state='Prepared'
    [IO.File]::WriteAllText($prepared.Receipt,($receipt|ConvertTo-Json))
    $null=& $installer -Action Restore -GameDir $cold -ReceiptId $prepared.ReceiptId
    Check (![IO.File]::Exists((Join-Path $cold 'OutRunDesktop.exe'))) 'prepared receipt can undo published new file'
    Store $exe (Fake-Exe 1)
    $locked=[IO.File]::Open($exe,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try { Refuses { & $installer -GameDir $game -PackageDir $package } 'cannot access|being used|access.*denied' } finally { $locked.Dispose() }
    Check ((Hash $exe) -eq $oldSha) 'locked destination retained'
    # A failed publication retains a recoverable Prepared receipt and removes
    # only its owned staging file. The unchanged old EXE needs no replacement.
    $history=Join-Path $game '.outrun-candidate-history';$before=@((Get-ChildItem -LiteralPath $history -Directory).Name)
    $attributes=[IO.File]::GetAttributes($exe);[IO.File]::SetAttributes($exe,$attributes -bor [IO.FileAttributes]::ReadOnly)
    try { Refuses { & $installer -GameDir $game -PackageDir $package } 'denied|read.only|permission' } finally { [IO.File]::SetAttributes($exe,$attributes) }
    Check ((Hash $exe) -eq $oldSha -and @(Get-ChildItem -LiteralPath $game -Filter '.OutRunDesktop.*.tmp').Count -eq 0) 'failed publication retains original and clears own stage'
    $new=@(Get-ChildItem -LiteralPath $history -Directory | Where-Object Name -NotIn $before)
    Check ($new.Count -eq 1) 'one interrupted transaction recorded'
    $interrupted=Get-Content -LiteralPath (Join-Path $new[0].FullName 'receipt.json') -Raw|ConvertFrom-Json
    Check ($interrupted.state -eq 'Prepared') 'prepared state retained on failed publication'
    $null=& $installer -Action Restore -GameDir ($game+[IO.Path]::DirectorySeparatorChar) -ReceiptId $new[0].Name
    Check ((Hash $exe) -eq $oldSha) 'prepared-before-publish recovery and trailing slash identity'
    $hardLink=Join-Path $root 'linked-executable'
    New-Item -ItemType HardLink -Path $hardLink -Value $exe -ErrorAction Stop|Out-Null
    try { Refuses { & $installer -GameDir $game -PackageDir $package } 'Hard-linked destination' } finally { [IO.File]::Delete($hardLink) }
    Check ((Hash $exe) -eq $oldSha) 'hard-link ownership not silently broken'
    $junction=Join-Path $root 'linked package'
    New-Item -ItemType Junction -Path $junction -Value $package -ErrorAction Stop|Out-Null
    Refuses { & $installer -GameDir $game -PackageDir $junction } 'Reparse/symlink'
    Check ((Hash $exe) -eq $oldSha) 'linked package refused without destination change'
    $passed=$true;"candidate installer: $checks checks PASS (synthetic files only; no game or owner install)"
} finally {
    if($passed) {
        $resolved=(Resolve-Path -LiteralPath $root).ProviderPath
        if(![String]::Equals($resolved,[IO.Path]::GetFullPath($root),[StringComparison]::OrdinalIgnoreCase) -or
            !$resolved.StartsWith(([IO.Path]::GetFullPath($parent)+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
            [IO.File]::ReadAllText((Join-Path $root 'owned.txt')) -ne $token) { throw 'Fixture cleanup scope/ownership check failed.' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    } else { Write-Host "Retained failed synthetic fixture: $root" }
}
