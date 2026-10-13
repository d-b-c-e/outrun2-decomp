"""Re-vendor the shared input shim from the wheel toolkit (vendor/SHIM.md).

  py -3 -I windows/tools/sync_shim.py --toolkit E:/Source/toolkits/dbce-wheel-mod-toolkit --commit <sha>

Copies native/shim/{shim,host,shim_dll,tests,docs} at that commit onto the same paths under windows/, byte for byte
from git (git show), and rewrites vendor/shim.lock.json with the commit and every file's blob id. Paths dropped by the
toolkit are listed, not deleted: remove them with git rm after review. Game-owned files beside the mirrors (for example
shim/wheelffb_bridge.*, host/control_publish.*) are never touched. Edit the shim in the toolkit, never here.
"""
import argparse
import json
import pathlib
import subprocess
import sys

WINDOWS = pathlib.Path(__file__).resolve().parents[1]
LOCK = WINDOWS / 'vendor' / 'shim.lock.json'
DIRS = ('shim', 'host', 'shim_dll', 'tests', 'docs')


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--toolkit', required=True, type=pathlib.Path)
    ap.add_argument('--commit', required=True)
    a = ap.parse_args()
    commit = git(a.toolkit, 'rev-parse', '--verify', a.commit + '^{commit}').decode().strip()
    rows = git(a.toolkit, 'ls-tree', '-r', commit, '--', *('native/shim/' + d for d in DIRS)).decode().splitlines()
    files = {}
    for row in rows:
        meta, path = row.split('\t', 1)
        mode, kind, blob = meta.split()
        if kind != 'blob' or mode not in ('100644', '100755'):
            sys.exit('refused: unexpected tree entry ' + row)
        rel = path[len('native/shim/'):]
        out = WINDOWS / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(git(a.toolkit, 'show', commit + ':' + path))
        files[rel] = blob
    old = json.loads(LOCK.read_text(encoding='utf-8'))['files'] if LOCK.exists() else {}
    dropped = sorted(set(old) - set(files))
    LOCK.write_text(json.dumps({'schema': 'dbce.vendor-lock@1', 'source': 'd-b-c-e/dbce-wheel-mod-toolkit',
                                'path': 'native/shim', 'commit': commit, 'files': dict(sorted(files.items()))},
                               indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'vendored {len(files)} files from toolkit {commit[:12]}; lock rewritten')
    for rel in dropped:
        print('no longer in the toolkit (git rm after review):', rel)


if __name__ == '__main__':
    main()
