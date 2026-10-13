"""FixC2CRankings (Tweaks hooks_bugfixes.cpp): every launcher-gated site of OR2006C2C.EXE is accounted for in the decomp.

The PC checks for its launcher with OpenEventA on the protection's named object ("v7_%04d", pid ^ 0x19EA3FD3). The
Steam build never creates it, so each check fails there and the code after it is skipped (rankings and ghost saves not
written); Tweaks' FixC2CRankings creates the event. The decomp has no launcher: each gated site must take the
launcher-present path, and say so where it does.

The sites are the EXE's `call [OpenEventA]` (IAT 5960F4) and the callers of its two wrappers, read from the Steam
build (sha256 68ceb386829066f8...) by a scan of FF 15 / E8 calls (nene 2026-10-13). Each entry names the decomp file
holding the site and the text that shows the present path; a site the decomp does not run says why.

  python -I launcher_gate_audit.py <repository root>
"""
import pathlib, sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
NOTE_447750 = 'The PC first checks its launcher'
SITES = [
    # (EXE function, what it is, decomp file, text that must be there)
    ('447750', 'OutRun record insert', 'src/platform/race_end_runtime.cpp', NOTE_447750),
    ('4478F0', 'arcade record insert', 'src/platform/race_end_runtime.cpp', NOTE_447750),
    ('447A60', 'Heart Attack record insert', 'src/platform/race_end_runtime.cpp', NOTE_447750),
    ('447C10', 'record insert', 'src/platform/race_end_runtime.cpp', NOTE_447750),
    ('447DC0', 'Time Attack record insert', 'src/platform/race_end_runtime.cpp', 'Same launcher note as 447750'),
    ('4480B0', 'Time Attack record insert', 'src/platform/race_end_runtime.cpp', 'Same launcher note as 447750'),
    ('447F60', 'C2C time record insert', 'src/platform/race_manager_runtime.cpp', 'the launcher check is not reproduced'),
    ('448230', 'multi-stage record insert', 'src/platform/race_variant_owners.cpp', 'The launcher check of the'),
    ('4483D0', 'C2C points record insert', 'src/platform/race_variant_owners.cpp', 'The launcher check of the'),
    ('465EC0', 'the check as a function (translated ghost module: 466DD0, 467A72)', 'src/platform/race_ghosts_runtime.cpp',
     'case 0x465ec0u:return 1u;'),
    ('4168D0', 'ghost save write, gated through 40E400 (416A1D, 416A36; translated 4677E7, 467A29)',
     'src/platform/race_ghosts.cpp', "That check is not reproduced"),
    ('4168D0', 'its translated callers', 'src/platform/race_ghosts_runtime.cpp', 'case 0x4168d0u:return ghost_save_write_4168d0('),
]
# Gated sites the decomp does not run, with the reason.
NOT_RUN = {
    '40E888': 'the Direct3D device error box (title text only); the port has its own device and messages',
}
failures = []
for fn, what, rel, text in SITES:
    p = root / rel
    if not p.is_file():
        failures.append('%s (%s): %s is missing' % (fn, what, rel)); continue
    if text not in p.read_text(encoding='utf-8', errors='replace'):
        failures.append('%s (%s): %s does not show the launcher-present path (%r)' % (fn, what, rel, text))
for f in failures: print('launcher gate audit: FAIL:', f)
if failures: sys.exit(1)
print('launcher gate audit: %d gated sites on the launcher-present path, %d not run (%s)' %
      (len(SITES), len(NOT_RUN), '; '.join('%s: %s' % kv for kv in NOT_RUN.items())))
