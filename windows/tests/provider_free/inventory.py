"""Read-only SHA256/Windows UTC filetime inventory; numeric ticks, never local date strings."""
import hashlib, json, os, pathlib, sys
EPOCH = 621355968000000000  # .NET DateTime UTC ticks at Unix epoch; seven decimal places retained.

def inventory(root):
    root = pathlib.Path(root)
    rows = {}
    for parent, dirs, files in os.walk(root, followlinks=False):
        dirs.sort(); files.sort()
        for name in dirs + files:
            p = pathlib.Path(parent) / name
            s = p.stat(follow_symlinks=False)
            if s.st_file_attributes & 0x400:
                raise ValueError(f"reparse point refused: {p}")
            if p.is_file():
                with p.open('rb') as stream:
                    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
                rows[p.relative_to(root).as_posix()] = dict(
                    sha256=digest,
                    bytes=s.st_size, writeUtcTicks=s.st_mtime_ns // 100 + EPOCH,
                    creationUtcTicks=s.st_birthtime_ns // 100 + EPOCH,
                    attributes=s.st_file_attributes)
    return rows

if __name__ == '__main__':
    pathlib.Path(sys.argv[2]).write_text(json.dumps(inventory(sys.argv[1]), indent=1, sort_keys=True))
