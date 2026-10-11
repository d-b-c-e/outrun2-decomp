# Run: python -I windows/tests/teardown_objects.py <repo root> [--clang-cl PATH] [--mutations]
#
# P21g, the compiler's half of the teardown audit (teardown_audit.py is the lexer's; windows/docs/P21-SHUTDOWN.md D2).
# The lexer cannot know which user types have destructors, nor see a call made through a macro, an alias or a function
# pointer (Astra 13114). Here clang-cl compiles every translation unit in windows/shim, and every header on its own, and:
#   R2 exit-time  -Werror=exit-time-destructors: any static, thread_local or namespace-scope object whose destructor
#                 would run at CRT exit fails to compile, whatever its spelling, nesting or type (a custom OwnedDevice
#                 included). The objects must also not reference atexit/_onexit/at_quick_exit or __tlregdtor, which is
#                 how such destructors, or an explicit registration, reach the CRT;
#   R1 stdio      the objects reference no console or FILE-stream entry point (fopen, fclose, fwrite, the vf*printf
#                 family, __acrt_iob_func, std::cout and the iostreams, filebuf). String formatting into a buffer
#                 (__stdio_common_vsprintf, vsscanf: snprintf, sscanf) touches no stream and is allowed;
#   R3 detach     no _Thrd_detach (std::thread::detach);
#   R4 ending     no exit, abort, ExitProcess, TerminateProcess, ExitThread, TerminateThread, _endthread[ex], ....
# Undefined symbols are what the compiled code calls after macros, aliases and std:: spellings are resolved.
# Out of scope, by design: fail-fast paths the compiler or library inserts and no source line asks for (std::terminate on
# an exception escaping noexcept, _invoke_watson, _purecall); and code no shim translation unit compiles or includes,
# which only the lexer sees. An exception needs an exact entry (file + symbol, or file + line for R2) with a reason in
# teardown_objects.json; an entry that no longer matches fails.
import json, os, pathlib, re, shutil, subprocess, sys, tempfile

SCOPE = "windows/shim"
INCLUDES = ["windows/shim", "windows/vendor/controls"]
DEFINES = ["NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", "UNICODE", "_UNICODE"]
FLAGS = ["/nologo", "/std:c++17", "/EHsc", "/c", "-Wno-everything", "-Werror=exit-time-destructors"]
EXIT_TIME = re.compile(r"^(.*?)\((\d+),\d+\):\s+error: declaration requires an exit-time destructor")

STREAM = {"__acrt_iob_func", "fopen", "_wfopen", "fopen_s", "_wfopen_s", "_fsopen", "_wfsopen", "freopen", "_wfreopen",
          "freopen_s", "_wfreopen_s", "_fdopen", "_wfdopen", "fclose", "_fcloseall", "fflush", "_flushall", "fwrite", "fread",
          "fputs", "fputws", "fgets", "fgetws", "fputc", "fputwc", "fgetc", "fgetwc", "putc", "putwc", "getc", "getwc",
          "putchar", "putwchar", "getchar", "getwchar", "puts", "_putws", "gets_s", "_getws_s", "ungetc", "ungetwc",
          "perror", "_wperror", "setvbuf", "setbuf", "fseek", "_fseeki64", "ftell", "_ftelli64", "rewind", "fgetpos",
          "fsetpos", "clearerr", "feof", "ferror", "tmpfile", "tmpfile_s", "_popen", "_wpopen", "_pclose", "_fileno",
          "_lock_file", "_unlock_file", "_fputchar", "_fgetchar"}
STREAM_PREFIX = ("__stdio_common_vf",)   # vfprintf(_s,_p), vfwprintf(...), vfscanf, vfwscanf
STREAM_PART = ("?cout@std@@", "?cerr@std@@", "?clog@std@@", "?cin@std@@", "?wcout@std@@", "?wcerr@std@@", "?wclog@std@@",
               "?wcin@std@@", "?$basic_filebuf@", "_Fiopen")
EXIT_TIME_SYMBOLS = {"atexit", "_onexit", "at_quick_exit", "__tlregdtor"}
DETACH = {"_Thrd_detach"}
ENDING = {"exit", "_exit", "_Exit", "quick_exit", "abort", "_cexit", "_c_exit", "_endthread", "_endthreadex"} | {
    p + s for s in ("ExitProcess", "TerminateProcess", "ExitThread", "TerminateThread", "FatalExit", "FatalAppExitA",
                    "FatalAppExitW", "RaiseFailFastException", "RtlExitUserProcess", "RtlExitUserThread", "NtTerminateProcess")
    for p in ("", "__imp_")}

def rule_of(symbol):
    if symbol in STREAM or symbol.startswith(STREAM_PREFIX) or any(p in symbol for p in STREAM_PART): return "R1 stdio"
    if symbol in EXIT_TIME_SYMBOLS: return "R2 exit-time destructor registration"
    if symbol in DETACH: return "R3 thread detach"
    if symbol in ENDING: return "R4 process or thread ending"
    return None

def tools(explicit):
    """clang-cl (given, CLANG_CL, a Visual Studio install with its LLVM component, or PATH) and llvm-nm beside it."""
    candidates = [explicit, os.environ.get("CLANG_CL")]
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.exists():
        out = subprocess.run([str(vswhere), "-products", "*", "-property", "installationPath"], capture_output=True, text=True).stdout
        candidates += [str(pathlib.Path(p.strip()) / "VC/Tools/Llvm/x64/bin/clang-cl.exe") for p in out.splitlines() if p.strip()]
    candidates.append(shutil.which("clang-cl"))
    for c in candidates:
        if c and pathlib.Path(c).is_file():
            nm = pathlib.Path(c).with_name("llvm-nm.exe")
            if nm.is_file(): return pathlib.Path(c), nm
    raise SystemExit("teardown objects: no clang-cl with llvm-nm beside it (pass --clang-cl)")

def units(tree):
    """Each .cpp, and each header compiled on its own through a stub, as (reported file, source to compile)."""
    for path in sorted((tree / SCOPE).rglob("*")):
        if path.suffix == ".cpp": yield path, path
        elif path.suffix in (".hpp", ".h"): yield path, None

def compile_and_scan(tree, clang, nm, only=None):
    """[(rule, file, where, text)]: where is a line (R2 compile) or a symbol. Raises on any other compile failure."""
    found = []
    with tempfile.TemporaryDirectory() as out:
        out = pathlib.Path(out)
        for i, (path, source) in enumerate(units(tree)):
            rel = path.relative_to(tree).as_posix()
            if only and rel != only: continue
            if source is None:
                source = out / ("header%d.cpp" % i); source.write_text('#include "%s"\n' % path.as_posix(), encoding="utf-8")
            obj = out / ("unit%d.obj" % i)
            cmd = [str(clang)] + FLAGS + ["/I" + str(tree / d) for d in INCLUDES] + ["/D" + d for d in DEFINES] + [str(source), "/Fo" + str(obj)]
            r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
            hits = [m for m in (EXIT_TIME.match(l.strip()) for l in (r.stdout + r.stderr).splitlines()) if m]
            for m in hits:
                f = pathlib.Path(m.group(1)).resolve()
                frel = f.relative_to(tree.resolve()).as_posix() if tree.resolve() in f.parents else f.as_posix()
                lines = f.read_text(encoding="utf-8", errors="replace").splitlines() if f.exists() else []
                n = int(m.group(2)); text = " ".join(lines[n - 1].split()) if 0 < n <= len(lines) else ""
                found.append(("R2 exit-time destructor", frel, n, text))
            if r.returncode != 0:
                if hits: continue   # the diagnostics are the finding; there is no object to scan
                raise RuntimeError("compile failed for %s:\n%s" % (rel, (r.stdout + r.stderr)[-2000:]))
            syms = subprocess.run([str(nm), "-u", str(obj)], capture_output=True, text=True, check=True).stdout.split()
            for s in sorted({s for s in syms if s != "U"}):
                rule = rule_of(s)
                if rule: found.append((rule, rel, s, s))
    return found

def audit(tree, clang, nm, allow_path, only=None):
    allow = json.loads(allow_path.read_text(encoding="utf-8")) if allow_path.exists() else []
    keys = {(a["file"], str(a["where"])) for a in allow}
    hits = compile_and_scan(tree, clang, nm, only)
    bad = [h for h in hits if (h[1], str(h[2])) not in keys]
    stale = [a for a in allow if not any(h[1] == a["file"] and str(h[2]) == str(a["where"]) for h in hits)] if not only else []
    return hits, bad, stale

# (rule, file, appended text, what must be flagged: a symbol, or the planted line for R2's compile check)
PLANTS = [
    ("R2 exit-time destructor", "windows/shim/core.cpp",
     "namespace planted {\n    struct OwnedDevice { ~OwnedDevice() {} int h = 0; };\n    OwnedDevice g_device;\n}", "OwnedDevice g_device;"),
    ("R2 exit-time destructor", "windows/shim/parse.cpp", "#include <vector>\ninline namespace planted_v1 { std::vector<int> g_slots; }",
     "inline namespace planted_v1 { std::vector<int> g_slots; }"),
    ("R2 exit-time destructor", "windows/shim/parse.cpp", "#include <string>\nthread_local std::string planted_tls;", "thread_local std::string planted_tls;"),
    ("R2 exit-time destructor", "windows/shim/core.cpp",
     "namespace { struct Owned { ~Owned() {} }; }\nint planted_local() { static Owned o; return 0; }", "int planted_local() { static Owned o; return 0; }"),
    ("R2 exit-time destructor registration", "windows/shim/parse.cpp", "#include <cstdlib>\nstatic int planted_reg = std::atexit([] {});", "atexit"),
    ("R1 stdio", "windows/shim/parse.cpp", '#include <cstdio>\nvoid planted_open() { if (std::FILE* f = std::fopen("x", "rb")) std::fclose(f); }', "fopen"),
    ("R1 stdio", "windows/shim/core.cpp", "#include <cstdio>\nauto planted_write = &std::fwrite;", "fwrite"),   # no call the lexer can see
    ("R1 stdio", "windows/shim/core.cpp", '#include <cstdio>\n#define PLANTED_SINK stderr\nvoid planted_log() { std::fputs("x", PLANTED_SINK); }', "fputs"),
    ("R1 stdio", "windows/shim/parse.cpp", "#include <iostream>\nvoid planted_cout() { std::cout << 1; }", "?cout@std@@"),
    ("R3 thread detach", "windows/shim/core.cpp", "#include <thread>\nvoid planted_detach() { std::thread([] {}).detach(); }", "_Thrd_detach"),
    ("R4 process or thread ending", "windows/shim/parse.cpp", "#include <cstdlib>\nvoid planted_end(int c) { if (c == 7) std::exit(c); }", "exit"),
    ("R4 process or thread ending", "windows/shim/dinput_provider.cpp", "void planted_kill() { ::TerminateProcess(::GetCurrentProcess(), 1); }", "__imp_TerminateProcess"),
]
# Allowed patterns, planted together: nothing may be flagged (no false positives on what workers legitimately use).
CLEAN = ("#include <atomic>\n#include <mutex>\n#include <cstdio>\n#include <thread>\nnamespace planted_ok {\n"
         "    std::atomic<int> counter{0};\n    std::mutex lock;\n    constexpr int k = 4;\n    struct Plain { int a; };\n    Plain plain;\n"
         "    static const char* const name = \"x\";\n}\n"
         "int planted_format(char* b, int n) { return std::snprintf(b, (size_t)n, \"%d\", planted_ok::k); }\n"
         "int planted_parse(const char* t) { int v = 0; return std::sscanf(t, \"%d\", &v); }\n"
         "void planted_join() { std::thread w([] {}); w.join(); }\n")

def mutations(root, clang, nm):
    ok = True
    def scratch(tmp):
        copy = pathlib.Path(tmp)
        for d in [SCOPE] + INCLUDES:
            if not (copy / d).exists(): shutil.copytree(root / d, copy / d)
        return copy
    for rule, rel, text, want in PLANTS:
        with tempfile.TemporaryDirectory() as tmp:
            copy = scratch(tmp); target = copy / rel
            target.write_text(target.read_text(encoding="utf-8") + "\n" + text + "\n", encoding="utf-8")
            _, bad, _ = audit(copy, clang, nm, root / "windows/tests/teardown_objects.json", only=rel)
            caught = any(b[0] == rule and b[1] == rel and (b[3] == want or (isinstance(b[2], str) and want in b[2])) for b in bad)
            print("mutation %-56s %s" % ("%s (%s) in %s" % (rule, want if len(want) < 24 else want[:21] + "...", rel), "caught" if caught else "MISSED: %r" % bad))
            ok &= caught
    with tempfile.TemporaryDirectory() as tmp:
        copy = scratch(tmp); target = copy / "windows/shim/core.cpp"
        target.write_text(target.read_text(encoding="utf-8") + "\n" + CLEAN, encoding="utf-8")
        _, bad, _ = audit(copy, clang, nm, root / "windows/tests/teardown_objects.json", only="windows/shim/core.cpp")
        print("mutation %-56s %s" % ("allowed patterns (atomic, mutex, snprintf, sscanf, join)", "clean" if not bad else "FALSE POSITIVE: %r" % bad))
        ok &= not bad
    with tempfile.TemporaryDirectory() as tmp:
        allow = pathlib.Path(tmp) / "allow.json"
        allow.write_text(json.dumps([{"file": "windows/shim/core.cpp", "where": "fopen", "reason": "stale"}]), encoding="utf-8")
        _, _, stale = audit(root, clang, nm, allow)
        print("mutation %-56s %s" % ("a stale allowlist entry", "caught" if stale else "MISSED")); ok &= bool(stale)
    return ok

def main():
    root = pathlib.Path(sys.argv[1]).resolve()
    explicit = sys.argv[sys.argv.index("--clang-cl") + 1] if "--clang-cl" in sys.argv else None
    clang, nm = tools(explicit)
    if "--mutations" in sys.argv: return 0 if mutations(root, clang, nm) else 1
    hits, bad, stale = audit(root, clang, nm, root / "windows/tests/teardown_objects.json")
    for b in bad: print("UNCLASSIFIED %s %s: %s" % (b[0], b[1], "line %d: %s" % (b[2], b[3]) if isinstance(b[2], int) else b[2]))
    for s in stale: print("STALE allowlist entry %s: %s" % (s["file"], s["where"]))
    n = sum(1 for _ in units(root))
    print("teardown objects: %d units in %s compiled by %s, %d findings, %d unclassified, %d stale" % (n, SCOPE, clang.name, len(hits), len(bad), len(stale)))
    return 1 if bad or stale else 0

if __name__ == "__main__": sys.exit(main())
