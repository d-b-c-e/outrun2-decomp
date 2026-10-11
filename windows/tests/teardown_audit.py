# Run: python -I windows/tests/teardown_audit.py <repo root> [--selftest | --mutations]
#
# P21g, the teardown audit (windows/docs/P21-SHUTDOWN.md D2/D4/D9; a sibling of write_audit.py, same pattern): the code
# a shim worker can reach (the provider worker's Core::service_devices and DInputProvider, the control worker's
# admission and batch parsing, their headers) must not:
#   R1 use stdio: stdout is runtime.log, which CRT exit closes, and fflush can block (H4);
#   R2 hold a static or thread_local object with a dynamic destructor: CRT exit runs it while a worker may still use it (H1);
#   R3 detach a thread: only the host may leave a worker running, and it owns that worker's state (D1, D9);
#   R4 end the process or a thread: ending is the host's D4 alone (exit, abort, ExitProcess, TerminateProcess, ...).
# Textual and conservative, like write_audit: an exception needs an exact entry (file + normalized line + count) with a
# reason in teardown_audit.json. Hits beyond an entry's count are findings, and an entry with fewer hits than its count
# fails as stale, so a duplicate of an allowed line is caught. The P20 recorder worker joins the scope when it is promoted
# from tests/ into windows/shim. windows/host (the production provider host) is in scope: it alone owns the destructor's
# two detaches (D9) and forced_exit's TerminateProcess (D4), allowed exactly there and nowhere else (Astra 15507).
import json, pathlib, re, shutil, subprocess, sys, tempfile

SCOPE = ["windows/shim", "windows/host"]
SUFFIXES = {".cpp", ".hpp", ".h"}
# R1: the console and every FILE-stream call (std:: or not). String formatting (snprintf, sscanf) touches no stream.
STDIO = re.compile(r"(?<![\w.>])(?:std::)?(printf|fprintf|vprintf|vfprintf|wprintf|fwprintf|vwprintf|vfwprintf|scanf|fscanf|wscanf|fwscanf|"
                   r"puts|_putws|fputs|fputws|putchar|putwchar|putc|putwc|fputc|fputwc|getchar|getwchar|getc|getwc|fgetc|fgetwc|fgets|fgetws|"
                   r"gets_s|ungetc|ungetwc|perror|_wperror|fflush|_flushall|fwrite|fread|fopen|_wfopen|fopen_s|_wfopen_s|_fsopen|_wfsopen|"
                   r"freopen|_wfreopen|freopen_s|_wfreopen_s|_fdopen|_wfdopen|fdopen|fclose|_fcloseall|setvbuf|setbuf|fseek|_fseeki64|ftell|"
                   r"_ftelli64|rewind|fgetpos|fsetpos|clearerr|feof|ferror|tmpfile|tmpfile_s|_popen|_wpopen|_pclose|_fileno)\s*\(|"
                   r"\bstd::(cout|cerr|clog|cin|wcout|wcerr|wclog|wcin)\b|\b(stdout|stderr|stdin)\b")
DTOR_TYPES = r"(?:string|wstring|vector|map|multimap|unordered_map|set|multiset|unordered_set|list|deque|shared_ptr|unique_ptr|function|mutex|recursive_mutex|shared_mutex|timed_mutex|condition_variable|condition_variable_any|thread|regex|ofstream|ifstream|fstream|stringstream|ostringstream|istringstream)"
STATIC = re.compile(r"^\s*(?:inline\s+)?(?:static|thread_local)\b[^(;]*\bstd::" + DTOR_TYPES + r"\b(?![^=;{]*\()")
# Applied only at namespace scope (tracked by braces, at any indentation): a definition of a standard type with a
# destructor. User types (a custom OwnedDevice) are beyond a lexer; teardown_objects.py has the compiler decide.
GLOBAL = re.compile(r"^(?:inline\s+)?(?:const\s+)?std::" + DTOR_TYPES + r"\b[^(;=]*\s\w+\s*(?:=|\{|;)")
DETACH = re.compile(r"\.\s*detach\s*\(")
ENDING = re.compile(r"(?<![\w.>:])(?:std::)?(exit|_exit|_Exit|quick_exit|abort|ExitProcess|TerminateProcess|ExitThread|TerminateThread)\s*\(")
RULES = [("R1 stdio", STDIO), ("R2 static with a dynamic destructor", STATIC), ("R2 global with a dynamic destructor", GLOBAL),
         ("R3 thread detach", DETACH), ("R4 process or thread ending", ENDING)]

def normalize(line): return re.sub(r"\s+", " ", re.sub(r"//.*$", "", line)).strip()

def code_lines(text):
    """The file with comments, string and character literals blanked, line for line, so braces and words inside them
    neither open scopes nor match a rule. Raw strings R"d(...)d" are handled; the line count is kept."""
    out, i, n, state = [], 0, len(text), None
    buf = []
    while i < n:
        c = text[i]
        if state is None:
            if text.startswith("//", i): j = text.find("\n", i); i = n if j < 0 else j; continue
            if text.startswith("/*", i):
                j = text.find("*/", i + 2); j = n if j < 0 else j + 2
                buf.append("".join("\n" if ch == "\n" else " " for ch in text[i:j])); i = j; continue
            m = re.match(r'(?:u8|u|U|L)?R"([^(\s]{0,16})\(', text[i:i + 24])
            if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
                end = text.find(")" + m.group(1) + '"', i + m.end()); end = n if end < 0 else end + len(m.group(1)) + 2
                buf.append('""' + "\n" * text.count("\n", i, end)); i = end; continue
            if c == "'" and 0 < i < n - 1 and text[i - 1].isdigit() and text[i + 1].isalnum(): buf.append(c); i += 1; continue  # 1'000
            if c in "\"'":
                q, j = c, i + 1
                while j < n and text[j] != q and text[j] != "\n": j += 2 if text[j] == "\\" else 1
                buf.append(q + q); i = j + 1; continue
        buf.append(c); i += 1
    return "".join(buf).split("\n")

NAMESPACE_OPEN = re.compile(r'(?:\bnamespace\b[\w\s:]*|\bextern\s*""\s*)$')   # namespace x {, inline namespace, extern "C" {

def scan(root):
    found = []
    for scope in SCOPE:
        for path in sorted((root / scope).rglob("*")):
            if path.suffix not in SUFFIXES: continue
            rel = path.relative_to(root).as_posix()
            raws = path.read_text(encoding="utf-8", errors="replace").splitlines()
            stack, head, macro = [], "", False   # scope kinds ('ns' or 'other'); text before the next brace; in a #define
            for n, code in enumerate(code_lines("\n".join(raws)), 1):
                at_namespace = all(k == "ns" for k in stack)   # at the start of this line
                line = normalize(code)
                if line:
                    for name, rx in RULES:
                        if rx is GLOBAL and not at_namespace: continue
                        if rx.search(line): found.append((name, rel, n, normalize(raws[n - 1]) if n <= len(raws) else line))
                directive = macro or line.startswith("#")   # preprocessor lines open no scope
                macro = directive and code.rstrip().endswith("\\")
                if directive: continue
                for ch in code:
                    if ch == "{": stack.append("ns" if NAMESPACE_OPEN.search(head.strip()) else "other"); head = ""
                    elif ch == "}":
                        if not stack: found.append(("R0 unbalanced braces", rel, n, normalize(raws[n - 1]))); stack = ["other"]  # fail closed
                        stack.pop(); head = ""
                    elif ch == ";": head = ""
                    else: head += ch
                head += " "
            if stack: found.append(("R0 unbalanced braces", rel, len(raws), "end of file with %d open scopes" % len(stack)))
    return found

def audit(root, allow_path):
    allow = json.loads(allow_path.read_text(encoding="utf-8")) if allow_path.exists() else []
    budget = {}
    for a in allow: budget[(a["file"], a["line"])] = budget.get((a["file"], a["line"]), 0) + int(a.get("count", 1))
    hits = scan(root)
    used, bad = {}, []
    for h in hits:
        key = (h[1], h[3]); used[key] = used.get(key, 0) + 1
        if used[key] > budget.get(key, 0): bad.append(h)   # not allowed, or beyond its exact count
    stale = [a for a in allow if used.get((a["file"], a["line"]), 0) < budget[(a["file"], a["line"])]]
    return hits, bad, stale

SELF = [  # (snippet, flagged rule or None)
    ('std::printf("x");', "R1 stdio"), ('fflush(stdout);', "R1 stdio"), ('std::cerr << x;', "R1 stdio"), ('fwrite(p,1,n,f);', "R1 stdio"),
    ('std::snprintf(t,sizeof t,"%d",1);', None), ('swprintf_s(b,L"x");', None),
    ('static std::string cache;', "R2 static with a dynamic destructor"), ('static const std::vector<int> table{1,2};', "R2 static with a dynamic destructor"),
    ('thread_local std::string scratch;', "R2 static with a dynamic destructor"), ('inline static std::mutex lock;', "R2 static with a dynamic destructor"),
    ('static std::string name(int i){return {};}', None), ('static constexpr int K=4;', None), ('static std::atomic<int> n{0};', None),
    ('std::atomic<void*> g_hook_user{nullptr};', None), ('std::string narrow(const wchar_t* w){return {};}', None),
    ('std::string g_name="x";', "R2 global with a dynamic destructor"), ('std::vector<int> g_slots;', "R2 global with a dynamic destructor"),
    ('inline std::vector<int> g_inline;', "R2 global with a dynamic destructor"),
    ('worker.detach();', "R3 thread detach"), ('t_ . detach ();', "R3 thread detach"),
    ('std::exit(3);', "R4 process or thread ending"), ('abort();', "R4 process or thread ending"), ('ExitProcess(0);', "R4 process or thread ending"),
    ('TerminateProcess(GetCurrentProcess(),1);', "R4 process or thread ending"), ('r.exit_code=0;', None), ('on_exit(x);', None), ('obj.abort();', None),
    # Astra 13114: FILE streams under any spelling; string formatting stays allowed.
    ('FILE* f=std::fopen("p","rb");', "R1 stdio"), ('std::fclose(f);', "R1 stdio"), ('fgets(b,n,f);', "R1 stdio"), ('::_wfopen_s(&f,p,L"rb");', "R1 stdio"),
    ('std::fputs("x",stderr);', "R1 stdio"), ('auto* s=stdout;', "R1 stdio"), ('std::sscanf(t,"%d",&v);', None), ('swscanf_s(t,L"%d",&v);', None),
    ('io.fclose(f);', None), ('// fopen("x") in a comment', None), ('auto m="fopen(";', None),
]
# Whole files, through scan(): R2's namespace-scope rule follows braces at any indentation (Astra 13114: an indented
# namespace and an inline namespace hid globals from the old column-0 heuristic). Each case lists (line, rule) hits.
SCOPE_CASES = [
    ("namespace outer {\n    std::vector<int> indented;\n}\n", [(2, "R2 global with a dynamic destructor")]),
    ("namespace a { inline namespace v1 {\n    std::string in_inline_ns;\n} }\n", [(2, "R2 global with a dynamic destructor")]),
    ('extern "C" {\n    std::string c_linkage;\n}\n', [(2, "R2 global with a dynamic destructor")]),
    ("namespace {\nstruct Device {\n    std::vector<int> member_;\n    std::string reason_;\n};\n}\n", []),   # class members
    ("void f() {\n    std::vector<int> local;\n}\n", []),                                                        # automatic
    ("namespace n {\nconst char* k = \"{\";\n    std::string after_brace_in_string;\n}\n", [(3, "R2 global with a dynamic destructor")]),
    ("namespace n {\n/* { */\n    std::string after_brace_in_comment;\n}\n", [(3, "R2 global with a dynamic destructor")]),
    ("#define OPEN {\nnamespace n {\n    std::string after_macro_brace;\n}\n", [(3, "R2 global with a dynamic destructor")]),
    ("namespace n {\nconstexpr long big = 1'000'000;\n    std::string after_separator;\n}\n", [(3, "R2 global with a dynamic destructor")]),
    ('namespace n {\nconst char* raw = R"x({ "a": 1 })x";\n    std::string after_raw;\n}\n', [(3, "R2 global with a dynamic destructor")]),
    ("namespace n {\nvoid f() {\n", [(2, "R0 unbalanced braces")]),   # the scope cannot be trusted: fail closed
    ("}\nstd::string g;\n", [(1, "R0 unbalanced braces"), (2, "R2 global with a dynamic destructor")]),
]

def selftest():
    bad = 0
    for snippet, want in SELF:   # one line at namespace scope
        got = next((name for name, rx in RULES if rx.search(normalize(code_lines(snippet)[0]))), None)
        if got != want: print("selftest: %r flagged %r, want %r" % (snippet, got, want)); bad += 1
    for text, want in SCOPE_CASES:
        with tempfile.TemporaryDirectory() as tmp:
            f = pathlib.Path(tmp) / SCOPE[0] / "case.cpp"; f.parent.mkdir(parents=True); f.write_text(text, encoding="utf-8")
            got = [(h[2], h[0]) for h in scan(pathlib.Path(tmp))]
        if got != want: print("selftest: %r flagged %r, want %r" % (text, got, want)); bad += 1
    print("teardown audit selftest: %d cases, %d wrong" % (len(SELF) + len(SCOPE_CASES), bad))
    return bad == 0

def mutations(root):
    # Each rule must catch a violation planted in a scratch copy of the real scanned tree; a stale allowlist entry must fail.
    # (rule, file, appended text, the line that must be flagged)
    plants = [("R1 stdio", "windows/shim/core.cpp", 'std::fprintf(stderr,"status\\n");', None),
              ("R1 stdio", "windows/shim/parse.cpp", 'void planted_open() { if (FILE* f = std::fopen("x", "rb")) std::fclose(f); }', None),
              ("R2 static with a dynamic destructor", "windows/shim/dinput_provider.cpp", "static std::string last_error;", None),
              ("R2 global with a dynamic destructor", "windows/shim/parse.cpp", "std::vector<int> g_lines;", None),
              ("R2 global with a dynamic destructor", "windows/shim/core.cpp", "namespace planted {\n    std::vector<int> g_indented;\n}", "std::vector<int> g_indented;"),
              ("R3 thread detach", "windows/shim/core.cpp", "std::thread([]{}).detach();", None),
              ("R4 process or thread ending", "windows/shim/dinput_provider.cpp", "ExitProcess(1);", None),
              # The host's exceptions are exact: another detach or ending anywhere in windows/host, or one more copy of
              # an allowed line (beyond its count), is a finding (Astra 15507).
              ("R3 thread detach", "windows/host/provider_host.hpp", "inline void planted_detach() { std::thread([]{}).detach(); }", None),
              ("R3 thread detach", "windows/host/provider_host.hpp", "if (worker_.joinable()) worker_.detach();", None),
              ("R4 process or thread ending", "windows/host/provider_host.hpp", "inline void planted_end() { TerminateProcess(GetCurrentProcess(), 1); }", None),
              ("R1 stdio", "windows/host/host_lifecycle.hpp", 'inline void planted_log() { std::fprintf(stderr, "x"); }', None),
              ("R2 global with a dynamic destructor", "windows/host/host_lifecycle.hpp", "std::vector<int> g_host_slots;", None)]
    ok = True
    for rule, rel, text, line in plants:
        line = line or text
        with tempfile.TemporaryDirectory() as tmp:
            copy = pathlib.Path(tmp)
            for scope in SCOPE: shutil.copytree(root / scope, copy / scope)
            target = copy / rel; target.write_text(target.read_text(encoding="utf-8") + "\n" + text + "\n", encoding="utf-8")
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
