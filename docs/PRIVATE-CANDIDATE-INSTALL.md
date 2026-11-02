# Private candidate executable install and restore

This PowerShell 7 tool replaces **only `OutRunDesktop.exe`** in an existing game folder. It does not install retail files, runtimes, WheelFfb, settings, saves, textures, LaunchBox entries or Wheelkit metadata, and it never launches the game. A successful file transaction is not clean-start, rendered, controls, force or owner acceptance. The ZIP stays private; this adds no public release approval.

Extract the candidate ZIP into its own folder. Close the destination game. From the extracted folder:

```powershell
pwsh -File ./windows/Install-Candidate.ps1 -GameDir 'D:/Games/OutRun Decomp'
```

The destination must already contain `OR2006C2C.cache` or `OR2006C2C.exe`; this is a folder marker check, not validation of the retail assets. The tool checks the private manifest's schema/platform/source identity, executable hash/length and AMD64 PE32+ identity. It refuses linked/reparse paths, hard-linked destinations and a running destination executable. Identical bytes return `AlreadyPresent` without a new transaction.

The manifest also lists the EXE's ordinary static import DLL names in `staticImports`. This is an inventory for the candidate operator, not a runtime prerequisite check: delay-load, transitive and dynamic dependencies (including optional force output) are outside that list. The installer neither loads those modules nor installs runtimes.

For a change, the tool retains the prior executable and a JSON receipt under `.outrun-candidate-history/<ReceiptId>/` in the game folder. It verifies the backup, writes a durable `Prepared` receipt, verifies a uniquely staged executable, then publishes it atomically and records `Installed`. Save the returned ReceiptId and keep the history directory. Settings, saves, assets, DLLs and unrelated files remain untouched. The tool does not infer that the executable was built from its manifest revision; retain the separate build/hash receipt.

To undo that transaction:

```powershell
pwsh -File ./windows/Install-Candidate.ps1 -Action Restore -GameDir 'D:/Games/OutRun Decomp' -ReceiptId '<ReceiptId returned by Install>'
```

Restore verifies both the currently installed hash and the backup before replacement. A newer/different executable or corrupted backup is retained with an error. If the transaction originally created a new native executable, restore removes only that receipt's verified executable. Repeating a completed restore is a no-op when its original state still matches. Receipts/backups stay retained. `Prepared` receipts can recover either side of the publication boundary, including a failed publication that left the prior executable unchanged. If restore published the original bytes (or original absence) but stopped before recording `Restored`, retry recognizes that exact prior state and completes only the receipt. A failure after install publication may leave `Prepared`; inspect/use its receipt rather than manually overwriting the target. Restore can use the source-checkout script too; it does not require the old package folder.

Offline qualification uses `windows/tests/Test-CandidateInstall.ps1` with fake executables and marker files in a uniquely owned ignored project folder. Its 47 checks cover exact restore, cold-executable removal, retained settings/saves/assets/DLLs, duplicate/no-op transactions, newer-file/corrupt-backup/tampered-package refusal, locked/read-only targets, Prepared and interrupted-restore recovery, literal bracket paths, trailing separators, junction and hard-link refusal, and staging cleanup. This is synthetic file evidence. Use the existing coordinated lease/muted/disposable route for any later owner install or game qualification; the installer itself performs neither.
