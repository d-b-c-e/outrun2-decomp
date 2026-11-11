# OutRun 2006 Coast 2 Coast — agent guide

Portfolio rules: `E:\Source\dbce-project-mgmt\HOW-WE-WORK.md` (read first; it replaces older process, lease, review and testing instructions). Current focus: `E:\Source\dbce-project-mgmt\NOW.md`. Feature status lives in the portfolio grid (`python E:\Source\dbce-project-mgmt\tools\portfolio_grid.py plan outrun2decomp`), not in this file. Grid id: `outrun2decomp`.

## Project

Native port of the PC OutRun 2006 Coast 2 Coast, reconstructed in C++ from the PC executable (part still translated from x86 by `tools/x86tr`, the rest hand-written). Platform layers for Switch, macOS, PS5, Xbox and a Windows desktop build (`windows/`, `OutRunDesktop.exe`). Fork of the public `outrun2-decomp` project (`d-b-c-e/outrun2-decomp`, PUBLIC; the owner pushes). Code only; the user's own Steam `OR2006C2C.EXE` and game folder are required.

## Build

- Windows desktop: `cmake -S . -B build-xbox-pc -G "Visual Studio 17 2022" -A x64 -T ClangCL -DOR2_XBOX=ON -DWHEELKIT_SDK=<wheelkit checkout> ...` then `cmake --build build-xbox-pc --config Release --target OutRunDesktop`. The full argument list (zlib, Vorbis, FFmpeg paths) is in `windows/README.md`. Output: `build-xbox-pc\windows\Release\OutRunDesktop.exe`.
- Other platforms: see `switch/`, `mac/`, `ps5/`, `xbox/` READMEs.

## Test

- Host runner and tests: `cmake -S . -B build -G Ninja -DOR2_EXE=<OR2006C2C.EXE> -DOR2_PC_ASSET_ROOT=<game folder>`, build, then `ctest --test-dir build`.
- Leak check: `tools/leak_check.py <OR2006C2C.EXE> <built binary> --allow tools/leak_check_allow.txt`.
- The port writes `OR2006C2C.cache`, `OutRunDesktop.ini`, `options.ini`, `runtime.log` and saves into the game folder. Back up the saves and ini files before a run and restore them after.

## Install / run on the rig

Copy `OutRunDesktop.exe` beside `OR2006C2C.EXE` in the game folder and start it from there (the folder must be writable).

## Shared code

The rig's build of this branch is kept outside the weekly-cleaned `_work`: configure with `-B E:\Source\temp\outrun-2006\build-wheelkit-runtime` (the Wheelkit launch check `games/outrun-2006/launch-check.json` starts `windows\Release\OutRunDesktop.exe` there); the source worktree can be recreated at any time.

Wheel input, the F6 panel, telemetry, the triple-screen layout and view matrices, and the cameras' framing and offsets come from the Wheelkit runtime: `wheelkit.dll` loaded beside the program through `wheelkit.h` (the `d-b-c-e/wheelkit` repository, `runtime/native/sdk`; the build takes both from `-DWHEELKIT_SDK=<checkout>`). Only `windows/source/wheelkit_runtime.cpp` (the loader and call sites) and the F6 pages (`wheelkit_panel.inc`) are ours here; change the runtime in Wheelkit. `[Controls]` in `OutRunDesktop.ini` is the only binding store.

## Project-specific rules

- No game data and no bytes of the original executable in the repo or in binaries (run the leak check).
- Retail DVD copies need the protection-free exe from OutRun2006Tweaks; the DVD's own EXE cannot be read.
- Keep upstream credits and `THIRD_PARTY_NOTICES.md`.

## Key docs

- `README.md`
- `windows/README.md`, `windows/ROADMAP.md`
- `windows/TOOLKIT-ADOPTION.md`
- `THIRD_PARTY_NOTICES.md`


<!-- BEGIN project-file-location -->
## Project files and temporary work

Keep project-related research, evidence, scripts, packages, and other working files inside the relevant project folder whenever practical. Keep disposable or machine-specific outputs git-ignored when they do not belong in version control; never ignore files that should be committed merely to hide them. When a project folder is unsuitable, use `E:\Source\temp\<project>` instead of creating scattered drive-root folders. Record the location of material needed by later work and update references when relocating it.

Clean up temporary files and disposable copies promptly once they are no longer useful. Preserve active work, user data, settings, saves, required evidence, backups, rollback artifacts, and worktrees until their retention or replacement is established. Follow existing project, rig, and data-loss safeguards; do not remove files merely because they are old or untracked.
<!-- END project-file-location -->