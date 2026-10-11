# Run: python -I windows/tests/write_audit.py <repo root> [--list | --selftest]
#
# The write audit for a latched test process (windows/docs/INPUT-SHIM.md section 15, slice 2b). A conservative
# tripwire, not a proof of a sandbox: every call that could write a file in the code linked into OutRunDesktop.exe must
# match a classified entry in write_audit.json exactly (file + the whole normalized line, so a destination change no
# longer matches); an unclassified call, or an entry that no longer matches anything, fails.
#
# Flagged: fopen/freopen/_wfopen/_fsopen/fdopen unless the mode is a visible read-only literal; every C++ output or
# read-write file stream; .open() with an output flag; Win32 file creation, move, replace, delete, copy, directories,
# attributes, links, WriteFile, private-profile writes and file mappings; C rename/remove/unlink/mkdir/rmdir/tmpfile;
# std::filesystem mutations; and calls to the writer wrappers below.
import json, pathlib, re, sys

# Everything linked into OutRunDesktop.exe: all of src (conservative), the Xbox core's sources and headers, windows/.
ROOTS = ["src", "xbox/source", "windows/source", "windows/shim"]
# Not linked into OutRunDesktop.exe: the offline tools, and the Xbox build's own UWP / Win32 entry points (the desktop
# build has its own, windows/source/window_desktop.cpp). ps5/source is not scanned: its library (outrun_ps5_platform)
# compiles only src/input, which is scanned under src.
EXCLUDE = ["src/tools/", "xbox/source/window_uwp.cpp", "xbox/source/window_win32.cpp"]
SUFFIXES = {".cpp", ".hpp", ".h", ".inc", ".cc"}
WRAPPERS = ["write_options", "write_checked", "exe_image_write_cache", "write_section", "make_directory",
            "remove_directory", "open_result_log", "prepare_run", "write_refusal", "write_png", "set_home"]

OPEN_RE = re.compile(r"\b(?:std::)?(fopen|_wfopen|freopen|_wfreopen|_fsopen|_wfsopen|fdopen|_fdopen)\s*\(")
STREAM_RE = re.compile(r"\b(?:std::)?(ofstream|wofstream|basic_ofstream|fstream|wfstream|basic_fstream|filebuf|wfilebuf|basic_filebuf)\b")
OPEN_FLAG_RE = re.compile(r"\.open\s*\(.*\bios(?:_base)?::(out|app|trunc)\b")
WIN_RE = re.compile(r"\b(CreateFile[AW]?|CreateFile2|MoveFile(?:Ex|WithProgress)?[AW]?|ReplaceFile[AW]?|DeleteFile[AW]?|"
                    r"CopyFile(?:Ex|2)?[AW]?|CreateDirectory(?:Ex)?[AW]?|RemoveDirectory[AW]?|SetFileAttributes[AW]?|"
                    r"CreateHardLink[AW]?|CreateSymbolicLink[AW]?|SHFileOperation[AW]?|WriteFile(?:Ex)?|SetEndOfFile|"
                    r"WritePrivateProfile\w*|CreateFileMapping[AW]?)\s*\(")
C_RE = re.compile(r"(?<![\w.>:])(?:std::)?(rename|remove|_unlink|unlink|_wremove|_wrename|_wunlink|_rmdir|_wrmdir|rmdir|"
                  r"_mkdir|_wmkdir|mkdir|tmpfile|tmpnam|_wtmpnam)\s*\(")
FS_RE = re.compile(r"\b(create_directory|create_directories|create_hard_link|create_symlink|create_directory_symlink|"
                   r"remove_all|copy_file|copy_symlink|resize_file|permissions|last_write_time)\s*\(|"
                   r"\b(?:filesystem|fs)::(remove|rename|copy)\s*\(")
# Readers too (2b review): every use of home() is classified, so moving home() to the run folder can never send a
# read-only game-data lookup to an empty folder. Game data is read through application_retail_root() / retail_root.
HOME_RE = re.compile(r"(?<![\w.>])(?:\w+::)?(home|application_home)\(")
WRAP_RE = re.compile(r"(?<![\w.>])(?:\w+::)*(" + "|".join(WRAPPERS) + r")\s*\(")

def normalize(line):
    return " ".join(line.strip().split())

def call_args(text, start):
    """The text inside the call's parentheses starting at text[start] == '(' (single line, best effort)."""
    depth, i = 0, start
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            i = j
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
        i += 1
    return text[start + 1:]

def last_arg(args):
    depth, cur, parts, i = 0, "", [], 0
    while i < len(args):
        c = args[i]
        if c == '"':
            j = i + 1
            while j < len(args) and args[j] != '"':
                j += 2 if args[j] == "\\" else 1
            cur += args[i:j + 1]
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
        i += 1
    parts.append(cur)
    return parts[-1].strip() if parts else ""

def read_only_open(line, m):
    args = call_args(line, m.end() - 1)
    mode = last_arg(args)
    if m.group(1) in ("fdopen", "_fdopen"):
        pass
    lit = re.fullmatch(r'L?"([^"]*)"', mode)
    if not lit:
        return False                      # a variable or computed mode: a candidate
    flags = lit.group(1)
    return "w" not in flags and "a" not in flags and "+" not in flags and flags.startswith("r")

def strip_comment_and_strings(line):
    out, i, in_str = [], 0, False
    while i < len(line):
        c = line[i]
        if in_str:
            if c == "\\":
                out.append("  ")
                i += 2
                continue
            if c == '"':
                in_str = False
            out.append(" " if c != '"' else '"')
        else:
            if line.startswith("//", i):
                break
            if c == '"':
                in_str = True
            out.append(c)
        i += 1
    return "".join(out)

def scan_text(rel, text):
    findings = []
    in_block = False
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw
        if in_block:
            end = line.find("*/")
            if end < 0:
                continue
            line = line[end + 2:]
            in_block = False
        while "/*" in line:
            a = line.find("/*")
            b = line.find("*/", a + 2)
            if b < 0:
                line = line[:a]
                in_block = True
                break
            line = line[:a] + line[b + 2:]
        code = strip_comment_and_strings(line)
        if not code.strip() or code.lstrip().startswith("#"):
            continue
        kinds = []
        for m in OPEN_RE.finditer(code):
            if not read_only_open(line, m):
                kinds.append(m.group(1))
        if STREAM_RE.search(code):
            kinds.append(STREAM_RE.search(code).group(1))
        if OPEN_FLAG_RE.search(line):
            kinds.append("open(out)")
        kinds += [m.group(1) for m in WIN_RE.finditer(code)]
        kinds += [m.group(1) for m in C_RE.finditer(code)]
        kinds += [m.group(1) or m.group(2) for m in FS_RE.finditer(code)]
        kinds += ["wrapper:" + m.group(1) for m in WRAP_RE.finditer(code)]
        kinds += ["home:" + m.group(1) for m in HOME_RE.finditer(code)]
        if kinds:
            findings.append({"file": rel, "line": normalize(raw), "number": number, "kinds": sorted(set(kinds))})
    return findings

def scan_tree(repo):
    out = []
    for root in ROOTS:
        base = repo / root
        if not base.exists():
            continue
        for p in sorted(base.rglob("*")):
            rel = p.relative_to(repo).as_posix()
            if p.suffix not in SUFFIXES or any(rel.startswith(e) for e in EXCLUDE):
                continue
            out += scan_text(rel, p.read_bytes().decode("utf-8", errors="replace"))
    return out

def audit(findings, entries):
    errors = []
    budget = {}
    for e in entries:
        key = (e["file"], normalize(e["line"]))
        budget[key] = budget.get(key, 0) + int(e.get("count", 1))
    used = {}
    for f in findings:
        key = (f["file"], f["line"])
        used[key] = used.get(key, 0) + 1
        if used[key] > budget.get(key, 0):
            errors.append(f"UNCLASSIFIED {f['file']}:{f['number']} [{', '.join(f['kinds'])}] {f['line']}")
    for key, n in budget.items():
        if used.get(key, 0) < n:
            errors.append(f"STALE ENTRY {key[0]}: {key[1]}")
    for e in entries:
        if e.get("class") not in CLASSES:
            errors.append(f"BAD CLASS {e.get('class')} for {e['file']}: {e['line']}")
    return errors

CLASSES = {
    "redirected": "derives from home() or the save folder; the run folder under the latch",
    "entry-redirected": "the latched entry point gives it the run folder",
    "latched-skip": "the code path does not run in a latched process",
    "latched-read-only": "in a latched process the game folder is only read here",
    "env-gated": "runs only with an OR2_ environment switch, all cleared by the latch",
    "fence": "the fence's own writes, under %LOCALAPPDATA%\\dbce\\outrun2006-decomp only",
    "wheel-off": "the 35a8aa3 wheel path, not run in a latched process",
    "not-a-file": "matches a pattern but writes no file",
    "in-memory": "a page-file mapping, no file on disk",
    "dev-hook": "development hook reading from home(); absent in the run folder, so it never starts there",
    "definition": "a writer wrapper's own definition or declaration line; its call sites are audited",
    "unreferenced": "a writer with no call site in the audited code; a new call is flagged as a wrapper call",
    "seeded-read": "reads writable state that the fence copies into the run folder (saves, options)",
    "home-definition": "home() itself, or where it is set (the desktop entry gives the latched run folder)",
}

# The guards the latched-skip / latched-read-only / wheel-off / entry-redirected classifications rely on. Each must exist
# (normalized line containing `text`), and where `before` is given, appear earlier in the same file than that text.
GUARDS = [
    ("xbox/source/platform_api.cpp", "if(const auto& run=outrun::xbox_runtime::latched_home();!run.empty())return run;", "if(!writable(retail_root))"),
    ("xbox/source/main.cpp", "if(outrun::xbox_runtime::latched_home().empty())outrun::platform::native_network_set_platform(", None),
    ("xbox/source/main.cpp", "exe_setup_prepare(retail_root,rt::home(),setup,&error,!outrun::xbox_runtime::latched_home().empty())", None),
    ("src/system/setup_screen.cpp", "if(!game_dir_read_only)adopt_former_cache(root,cache,fallback);", None),
    ("src/system/setup_screen.cpp", "if((game_dir_read_only||!exe_image_write_cache(cache,image,&e))&&", None),
    ("windows/source/window_desktop.cpp", "if(fence::latch_requested(dbce)){", "std::freopen(log.c_str(),\"w\",stdout)"),
    ("windows/source/window_desktop.cpp", "cleared=fence::clear_development_switches();", "std::freopen(log.c_str(),\"w\",stdout)"),
    ("windows/source/window_desktop.cpp", "load_desktop_settings(folder,!plan.latched)", None),
    ("windows/source/window_desktop.cpp", "if(!plan.latched){", "outrun::desktop::wheel_input_configure(settings);"),
    ("windows/source/desktop_settings.cpp", "if(!create_if_missing){", "std::fopen(own.c_str(),\"wb\")"),
]

def check_guards(repo, texts=None):
    errors = []
    for rel, text, before in GUARDS:
        src = texts.get(rel) if texts and rel in texts else (repo / rel).read_bytes().decode("utf-8")
        lines = [normalize(l) for l in src.splitlines()]
        at = next((i for i, l in enumerate(lines) if text in l), None)
        if at is None:
            errors.append(f"MISSING GUARD {rel}: {text}")
            continue
        if before:
            later = next((i for i, l in enumerate(lines) if before in l), None)
            if later is None or later < at:
                errors.append(f"GUARD OUT OF ORDER {rel}: {text} must come before {before}")
    return errors

def selftest(repo, entries):
    """The audit's own mutations: each must fail; the unmodified tree must pass."""
    base = scan_tree(repo)
    cases = []
    def mutated(rel, transform):
        text = (repo / rel).read_bytes().decode("utf-8")
        new = transform(text)
        assert new != text, rel
        others = [f for f in base if f["file"] != rel]
        return others + scan_text(rel, new)
    rank = "src/platform/race_end_runtime.cpp"
    cases.append(("an extra writer", mutated(rank, lambda t: t + '\nstatic void x(){std::FILE* f=std::fopen("evil.txt","wb");}\n')))
    cases.append(("a dynamic mode", mutated(rank, lambda t: t + "\nstatic void y(const char* p,const char* m){std::fopen(p,m);}\n")))
    cases.append(("a temp rename/remove", mutated(rank, lambda t: t + "\nstatic void z(){std::rename(\"a.tmp\",\"a\");std::remove(\"a.tmp\");}\n")))
    cases.append(("a direct game-root path", mutated(rank, lambda t: t.replace('std::fopen((dir+"/rankings.dat").c_str(),"wb")',
                                                                    'std::fopen((application_retail_root()+"/rankings.dat").c_str(),"wb")'))))
    cases.append(("an output stream", mutated(rank, lambda t: t + "\nstatic void s(){std::ofstream o(\"x.txt\");}\n")))
    cases.append(("an open with ios::out", mutated(rank, lambda t: t + "\nstatic void o(std::fstream& f){f.open(\"x\",std::ios::out);}\n")))
    cases.append(("a new wrapper call", mutated(rank, lambda t: t + "\nstatic void w(){write_options(\"x.ini\",{});}\n")))
    cases.append(("a Win32 delete", mutated(rank, lambda t: t + "\nstatic void d(){DeleteFileW(L\"x\");}\n")))
    cases.append(("a filesystem mutation", mutated(rank, lambda t: t + "\nstatic void m(){std::filesystem::remove_all(\"x\");}\n")))
    cases.append(("a game-data read through home()", mutated(rank, lambda t: t + "\nstatic void h(){auto p=home()+\"/data/course.pak\";}\n")))
    cases.append(("a removed writer (stale entry)", mutated(rank, lambda t: t.replace('std::fopen((dir+"/rankings.dat").c_str(),"wb")', 'nullptr'))))
    failures = []
    if audit(base, entries) or check_guards(repo):
        failures.append("the unmodified tree does not pass")
    api = "xbox/source/platform_api.cpp"
    api_text = (repo / api).read_bytes().decode("utf-8")
    guard_cases = [
        ("the latched home guard removed", {api: api_text.replace("if(const auto& run=outrun::xbox_runtime::latched_home();!run.empty())return run;", "")}),
        ("the LAN guard removed", {"xbox/source/main.cpp": (repo / "xbox/source/main.cpp").read_bytes().decode("utf-8").replace(
            "if(outrun::xbox_runtime::latched_home().empty())outrun::platform::native_network_set_platform(", "outrun::platform::native_network_set_platform(")}),
    ]
    for name, texts in guard_cases:
        if not check_guards(repo, texts):
            failures.append("NOT CAUGHT: " + name)
    for name, findings in cases:
        if not audit(findings, entries):
            failures.append("NOT CAUGHT: " + name)
    ro = scan_text("x.cpp", 'void r(const char* p){std::fopen(p,"rb");std::fopen(p,"r");}\n')
    if ro:
        failures.append("a read-only fopen was flagged")
    return failures, len(cases)

def main():
    repo = pathlib.Path(sys.argv[1])
    entries = json.loads((repo / "windows/tests/write_audit.json").read_text(encoding="utf-8")) if (repo / "windows/tests/write_audit.json").exists() else []
    if "--list" in sys.argv:
        for f in scan_tree(repo):
            print(f"{f['file']}:{f['number']} [{', '.join(f['kinds'])}] {f['line']}")
        return 0
    if "--selftest" in sys.argv:
        failures, n = selftest(repo, entries)
        for f in failures:
            print(f)
        print(f"write audit selftest: {n + 2} mutations, {len(failures)} failure(s)")
        return 1 if failures else 0
    errors = audit(scan_tree(repo), entries) + check_guards(repo)
    for e in errors:
        print(e)
    print(f"write audit: {len(entries)} classified entr{'y' if len(entries) == 1 else 'ies'}, {len(errors)} problem(s)")
    return 1 if errors else 0

if __name__ == "__main__":
    sys.exit(main())
