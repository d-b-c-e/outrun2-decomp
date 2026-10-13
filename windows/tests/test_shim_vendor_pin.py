"""ctest shim_vendor_pin: the shim files under windows/ are the toolkit's native/shim, unedited (vendor/SHIM.md).

Each file in vendor/shim.lock.json must hash to its pinned git blob id. A local edit fails here: change the toolkit,
then re-vendor with tools/sync_shim.py. Uses `git hash-object` (the repository's own line-ending normalization) when
run in a checkout, else the LF-normalized content.
"""
import hashlib
import json
import pathlib
import subprocess
import sys

WINDOWS = pathlib.Path(__file__).resolve().parents[1]
REPO = WINDOWS.parent


def blob_id(path, rel):
    try:
        out = subprocess.run(['git', '-C', str(REPO), 'hash-object', '--path=windows/' + rel, str(path)],
                             check=True, capture_output=True, text=True).stdout.strip()
        if len(out) == 40:
            return out
    except (OSError, subprocess.CalledProcessError):
        pass
    data = path.read_bytes().replace(b'\r\n', b'\n')
    return hashlib.sha1(b'blob %d\0' % len(data) + data).hexdigest()


def main():
    lock = json.loads((WINDOWS / 'vendor' / 'shim.lock.json').read_text(encoding='utf-8'))
    if lock.get('schema') != 'dbce.vendor-lock@1' or lock.get('path') != 'native/shim' or len(lock.get('commit', '')) != 40:
        sys.exit('shim_vendor_pin: unexpected lock header')
    files = lock['files']
    if not files:
        sys.exit('shim_vendor_pin: the lock lists no files')
    bad = []
    for rel, want in sorted(files.items()):
        path = WINDOWS / rel
        if not path.is_file():
            bad.append(rel + ': missing')
        elif blob_id(path, rel) != want:
            bad.append(rel + ': edited here (blob differs from toolkit ' + lock['commit'][:12] + ')')
    if bad:
        print('shim_vendor_pin: FAIL. The shim is the toolkit\'s native/shim; edit it there and run tools/sync_shim.py:')
        for b in bad:
            print('  ' + b)
        return 1
    print(f'shim_vendor_pin: {len(files)} files equal toolkit native/shim {lock["commit"][:12]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
