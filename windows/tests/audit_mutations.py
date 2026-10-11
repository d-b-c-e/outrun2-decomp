# Run: python -I windows/tests/audit_mutations.py <repo root>
#
# The write audit's own mutation check (slice 2b.3): breaks each argument and joining rule in a scratch copy of
# write_audit.py and requires its --selftest to fail. The copy reads write_audit.json from the repo root.
import pathlib, subprocess, sys, tempfile
repo = pathlib.Path(sys.argv[1])
src = (repo / "windows/tests/write_audit.py").read_text(encoding="utf-8")
scratch = tempfile.TemporaryDirectory()
out = pathlib.Path(scratch.name) / "audit_mutant.py"
MUTATIONS = [
    ("flags searched across all arguments",
     "    flags = argument(args, DESC_FLAGS[name])\n",
     "    flags = \"|\".join(args)\n"),
    ("unknown flag tokens accepted",
     "return bool(t) and all(x in READ_ONLY_DESC_FLAGS for x in t) and any(x.endswith(\"O_RDONLY\") for x in t)",
     "return any(x.endswith(\"O_RDONLY\") for x in t)"),
    ("unknown stream mode tokens accepted",
     "return bool(t) and all(t) and any(x.group(1) == \"in\" for x in t)",
     "return any(x and x.group(1) == \"in\" for x in t)"),
    ("fopen mode taken from the last argument",
     "    mode = argument(args, FOPEN_MODE[name])\n",
     "    mode = args[-1] if args else None\n"),
    ("argument count not checked",
     "    return args[index] if len(args) in counts else None\n",
     "    return args[index] if len(args) > index else None\n"),
    ("calls not followed to their close (the line's own parentheses only)",
     [("            end = max(end, last)\n", "            pass\n")]),
    ("a silent 12-line truncation (the 26e8d47 behaviour)",
     [("MAX_JOIN = 100\n", "MAX_JOIN = 12\n"),
      ("        if depth > 0:\n            incomplete = True\n", ""),
      ("            if close < 0 or last - i > MAX_JOIN:\n", "            if close < 0:\n"),
      ("            end = max(end, last)\n", "            end = max(end, min(last, i + MAX_JOIN))\n")]),
    ("incomplete findings classifiable",
     "        if f.get(\"incomplete\"):\n",
     "        if False:\n"),
]
caught = 0
for mutation in MUTATIONS:
    name, pairs = (mutation[0], mutation[1]) if len(mutation) == 2 else (mutation[0], [(mutation[1], mutation[2])])
    text = src
    for old, new in pairs:
        assert text.count(old) == 1, name
        text = text.replace(old, new)
    out.write_text(text, encoding="utf-8")
    r = subprocess.run([sys.executable, "-I", str(out), str(repo), "--selftest"], capture_output=True, text=True)
    ok = r.returncode != 0
    caught += ok
    first = next((l for l in r.stdout.splitlines() if l and not l.startswith("write audit selftest")), "")
    print(f"- {name}: {'caught: ' + first if ok else 'NOT CAUGHT'}")
scratch.cleanup()
print(f"{caught} of {len(MUTATIONS)} caught")
sys.exit(0 if caught == len(MUTATIONS) else 1)
