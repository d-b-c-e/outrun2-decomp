# Run: python -I windows/tests/teardown_audit.py <repo root> [--selftest | --mutations]
#
# P21g, the teardown audit (windows/docs/P21-SHUTDOWN.md D2/D4/D9; a sibling of write_audit.py, same pattern): the code
# a shim worker can reach (the provider worker's Core::service_devices and DInputProvider, the control worker's
# admission and batch parsing, their headers) must not:
#   R1 use stdio: stdout is runtime.log, which CRT exit closes, and fflush can block (H4);
#   R2 hold a static or thread_local object with a dynamic destructor: CRT exit runs it while a worker may still use it (H1);
#   R3 detach a thread: only the host may leave a worker running, and it owns that worker's state (D1, D9);
#   R4 end the process or a thread: ending is the host's D4 alone (exit, abort, ExitProcess, TerminateProcess, ...).
# Textual and conservative, like write_audit: an exception needs an exact entry (file + normalized line) with a reason in
# teardown_audit.json, and an entry that no longer matches anything fails. The P20 recorder worker joins the scope when it
# is promoted from tests/ into windows/shim.
import json, pathlib, re, shutil, subprocess, sys, tempfile

SCOPE = ["windows/shim"]
SUFFIXES = {".cpp", ".hpp", ".h"}
STDIO = re.compile(r"\b(?:std::)?(printf|fprintf|vprintf|vfprintf|wprintf|fwprintf|puts|fputs|putchar|perror|fflush|fwrite)\s*\(|\bstd::(cout|cerr|clog|wcout|wcerr|wclog)\b")
DTOR_TYPES = r"(?:string|wstring|vector|map|multimap|unordered_map|set|multiset|unordered_set|list|deque|shared_ptr|unique_ptr|function|mutex|recursive_mutex|shared_mutex|timed_mutex|condition_variable|condition_variable_any|thread|regex|ofstream|ifstream|fstream|stringstream|ostringstream|istringstream)"
STATIC = re.compile(r"^\s*(?:inline\s+)?(?:static|thread_local)\b[^(;]*\bstd::" + DTOR_TYPES + r"\b(?![^=;{]*\()")
GLOBAL = re.compile(r"^(?:const\s+)?std::" + DTOR_TYPES + r"\b[^(;=]*\s\w+\s*(?:=|\{|;)")
DETACH = re.compile(r"\.\s*detach\s*\(")
ENDING = re.compile(r"(?<![\w.>:])(?:std::)?(exit|_exit|_Exit|quick_exit|abort|ExitProcess|TerminateProcess|ExitThread|TerminateThread)\s*\(")
RULES = [("R1 stdio", STDIO), ("R2 static with a dynamic destructor", STATIC), ("R2 global with a dynamic destructor", GLOBAL),
         ("R3 thread detach", DETACH), ("R4 process or thread ending", ENDING)]

def normalize(line): return re.sub(r"\s+", " ", re.sub(r"//.*$", "", line)).strip()

def scan(root):
    found = []
    for scope in SCOPE:
        for path in sorted((root / scope).rglob("*")):
            if path.suffix not in SUFFIXES: continue
            rel = path.relative_to(root).as_posix()
            for n, raw in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                line = normalize(raw)
                if not line: continue
                at_column_0 = raw[:1] not in (" ", "\t")   # namespace scope in this code style; members are indented
                for name, rx in RULES:
                    if rx is GLOBAL and not at_column_0: continue
                    if rx.search(line): found.append((name, rel, n, line))
    return found

def audit(root, allow_path):
    allow = json.loads(allow_path.read_text(encoding="utf-8")) if allow_path.exists() else []
    keys = {(a["file"], a["line"]) for a in allow}
    hits = scan(root)
    bad = [h for h in hits if (h[1], h[3]) not in keys]
    stale = [a for a in allow if not any(h[1] == a["file"] and h[3] == a["line"] for h in hits)]
    return hits, bad, stale

SELF = [  # (snippet, flagged rule or None)
    ('std::printf("x");', "R1 stdio"), ('fflush(stdout);', "R1 stdio"), ('std::cerr << x;', "R1 stdio"), ('fwrite(p,1,n,f);', "R1 stdio"),
    ('std::snprintf(t,sizeof t,"%d",1);', None), ('swprintf_s(b,L"x");', None),
    ('static std::string cache;', "R2 static with a dynamic destructor"), ('static const std::vector<int> table{1,2};', "R2 static with a dynamic destructor"),
    ('thread_local std::string scratch;', "R2 static with a dynamic destructor"), ('inline static std::mutex lock;', "R2 static with a dynamic destructor"),
    ('static std::string name(int i){return {};}', None), ('static constexpr int K=4;', None), ('static std::atomic<int> n{0};', None),
    ('std::atomic<void*> g_hook_user{nullptr};', None), ('std::string narrow(const wchar_t* w){return {};}', None),
    ('std::string g_name="x";', "R2 global with a dynamic destructor"), ('std::vector<int> g_slots;', "R2 global with a dynamic destructor"),
    ('    std::vector<int> member_;', None), ('    std::string reason_;', None),   # indented: class members, not namespace globals
    ('worker.detach();', "R3 thread detach"), ('t_ . detach ();', "R3 thread detach"),
    ('std::exit(3);', "R4 process or thread ending"), ('abort();', "R4 process or thread ending"), ('ExitProcess(0);', "R4 process or thread ending"),
    ('TerminateProcess(GetCurrentProcess(),1);', "R4 process or thread ending"), ('r.exit_code=0;', None), ('on_exit(x);', None), ('obj.abort();', None),
]

def selftest():
    bad = 0
    for snippet, want in SELF:
        got = next((name for name, rx in RULES if (rx is not GLOBAL or snippet[:1] not in (" ", "\t")) and rx.search(normalize(snippet))), None)
        if got != want: print("selftest: %r flagged %r, want %r" % (snippet, got, want)); bad += 1
    print("teardown audit selftest: %d cases, %d wrong" % (len(SELF), bad))
    return bad == 0

def mutations(root):
    # Each rule must catch a violation planted in a scratch copy of the real scanned tree; a stale allowlist entry must fail.
    plants = [("R1 stdio", "windows/shim/core.cpp", 'std::fprintf(stderr,"status\\n");'),
              ("R2 static with a dynamic destructor", "windows/shim/dinput_provider.cpp", "static std::string last_error;"),
              ("R2 global with a dynamic destructor", "windows/shim/parse.cpp", "std::vector<int> g_lines;"),
              ("R3 thread detach", "windows/shim/core.cpp", "std::thread([]{}).detach();"),
              ("R4 process or thread ending", "windows/shim/dinput_provider.cpp", "ExitProcess(1);")]
    ok = True
    for rule, rel, line in plants:
        with tempfile.TemporaryDirectory() as tmp:
            copy = pathlib.Path(tmp)
            for scope in SCOPE: shutil.copytree(root / scope, copy / scope)
            target = copy / rel; target.write_text(target.read_text(encoding="utf-8") + "\n" + line + "\n", encoding="utf-8")
            _, bad, _ = audit(copy, root / "windows/tests/teardown_audit.json")
            caught = any(b[0] == rule and b[1] == rel and b[3] == normalize(line) for b in bad)
            print("mutation %-40s %s" % (rule + " in " + rel, "caught" if caught else "MISSED")); ok &= caught
    with tempfile.TemporaryDirectory() as tmp:
        allow = pathlib.Path(tmp) / "allow.json"
        allow.write_text(json.dumps([{"file": "windows/shim/core.cpp", "line": "std::printf(\"gone\");", "reason": "stale"}]), encoding="utf-8")
        _, _, stale = audit(root, allow)
        print("mutation %-40s %s" % ("a stale allowlist entry", "caught" if stale else "MISSED")); ok &= bool(stale)
    return ok

def main():
    root = pathlib.Path(sys.argv[1]).resolve()
    if "--selftest" in sys.argv: return 0 if selftest() else 1
    if "--mutations" in sys.argv: return 0 if (selftest() and mutations(root)) else 1
    hits, bad, stale = audit(root, root / "windows/tests/teardown_audit.json")
    for b in bad: print("UNCLASSIFIED %s %s:%d: %s" % b)
    for s in stale: print("STALE allowlist entry %s: %s" % (s["file"], s["line"]))
    files = sum(1 for scope in SCOPE for p in (root / scope).rglob("*") if p.suffix in SUFFIXES)
    print("teardown audit: %d files in %s, %d candidate lines, %d unclassified, %d stale" % (files, ", ".join(SCOPE), len(hits), len(bad), len(stale)))
    return 1 if bad or stale else 0

if __name__ == "__main__": sys.exit(main())
