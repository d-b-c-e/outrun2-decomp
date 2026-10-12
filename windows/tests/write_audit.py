# Run: python -I windows/tests/write_audit.py <repo root> [--list | --selftest]
#
# The write audit for a latched test process (windows/docs/INPUT-SHIM.md section 15, slice 2b). A conservative
# tripwire, not a proof of a sandbox: every call that could write a file in the code linked into OutRunDesktop.exe must
# match a classified entry in write_audit.json exactly (file + the whole normalized line, so a destination change no
# longer matches); an unclassified call, or an entry that no longer matches anything, fails.
#
# Flagged: the fopen family (including fopen_s / _wfopen_s / freopen_s) unless its mode argument is a read-only
# literal; CRT descriptor opens (_open, _wopen, _sopen[_s], _wsopen[_s], _creat, open) unless their flags argument is
# made only of known read-only flags including O_RDONLY; every member .open() unless its mode argument is made only of
# ios::in and ios::binary; every C++ output or read-write file stream; Win32 file creation, move, replace, delete, copy,
# directories, attributes, links, WriteFile, private-profile writes and file mappings; C rename/remove/unlink/mkdir/
# rmdir/tmpfile; std::filesystem mutations; writer wrappers; home() uses.
# Only the argument at the function's own mode or flags position is read (2b.2 audit review): a read-only token in the
# filename, or a read-only flag OR'd with anything unknown, leaves the call a candidate, and so does an argument count
# the function does not take. The key is the whole call: every call on the line is followed to its closing parenthesis
# and the key spans all of its lines, so a changed destination on any later line no longer matches. A call that does not
# close within MAX_JOIN lines is INCOMPLETE, an error no entry can classify.
# An ofstream adds ios::out to any mode it is opened with; every ofstream is flagged where it is declared, and that
# classification covers its opens. Textual matching only: an allowlisted writer still needs the actual write-destination
# fixture (test_fence.cpp); argument dataflow outside the call is not established here.
import bisect, json, pathlib, re, sys

# Everything linked into OutRunDesktop.exe: all of src (conservative), the Xbox core's sources and headers, windows/.
ROOTS = ["src", "xbox/source", "windows/source", "windows/shim", "windows/host", "windows/vendor/session", "windows/shim_dll"]
# Not linked into OutRunDesktop.exe: the offline tools, and the Xbox build's own UWP / Win32 entry points (the desktop
# build has its own, windows/source/window_desktop.cpp). ps5/source is not scanned: its library (outrun_ps5_platform)
# compiles only src/input, which is scanned under src. The standalone command publisher (control_publish.exe, b173a48:
# run by the first-loop runner as its own process) is not linked either; no audited file may include its header, so a
# later link into the game is refused here rather than left unscanned.
EXCLUDE = ["src/tools/", "xbox/source/window_uwp.cpp", "xbox/source/window_win32.cpp",
           "windows/host/control_publish.cpp", "windows/host/control_publish.hpp"]
STANDALONE_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](?:[^>"]*/)?control_publish\.hpp[>"]', re.M)
SUFFIXES = {".cpp", ".hpp", ".h", ".inc", ".cc"}
WRAPPERS = ["write_options", "write_checked", "exe_image_write_cache", "write_section", "make_directory",
            "remove_directory", "open_result_log", "prepare_run", "write_refusal", "write_png", "set_home"]

OPEN_RE = re.compile(r"\b(?:std::)?(fopen|_wfopen|fopen_s|_wfopen_s|freopen|_wfreopen|freopen_s|_wfreopen_s|_fsopen|_wfsopen|fdopen|_fdopen)\s*\(")
# CRT descriptor opens (2b audit review): flagged unless the flags are visibly read only (O_RDONLY, no write flag).
DESC_RE = re.compile(r"(?<![\w.>:])(?:::)?(_open|_wopen|_sopen|_sopen_s|_wsopen|_wsopen_s|_creat|_wcreat|creat|open)\s*\(")
STREAM_RE = re.compile(r"\b(?:std::)?(ofstream|wofstream|basic_ofstream|fstream|wfstream|basic_fstream|filebuf|wfilebuf|basic_filebuf)\b")
# Any member .open() (a stream's mode may be a variable; 2b audit review): flagged unless its mode is visibly
# std::ios::in with no out/app/trunc. Non-file .open() calls are classified as not-a-file.
MEMBER_OPEN_RE = re.compile(r"(?:\.|->)\s*open\s*\(")
WIN_RE = re.compile(r"\b(CreateFile[AW]?|CreateFile2|MoveFile(?:Ex|WithProgress)?[AW]?|ReplaceFile[AW]?|DeleteFile[AW]?|"
                    r"CopyFile(?:Ex|2)?[AW]?|CreateDirectory(?:Ex)?[AW]?|RemoveDirectory[AW]?|SetFileAttributes[AW]?|"
                    r"CreateHardLink[AW]?|CreateSymbolicLink[AW]?|SHFileOperation[AW]?|WriteFile(?:Ex)?|SetEndOfFile|"
                    r"WritePrivateProfile\w*|CreateFileMapping[AW]?)\s*\(")
C_RE = re.compile(r"(?<![\w.>:])(?:std::)?(rename|remove|_unlink|unlink|_wremove|_wrename|_wunlink|_rmdir|_wrmdir|rmdir|"
                  r"_mkdir|_wmkdir|mkdir|tmpfile|tmpfile_s|tmpnam|tmpnam_s|_wtmpnam|_wtmpnam_s)\s*\(")
FS_RE = re.compile(r"\b(create_directory|create_directories|create_hard_link|create_symlink|create_directory_symlink|"
                   r"remove_all|copy_file|copy_symlink|resize_file|permissions|last_write_time)\s*\(|"
                   r"\b(?:filesystem|fs)::(remove|rename|copy)\s*\(")
# Readers too (2b review): every use of home() is classified, so moving home() to the run folder can never send a
# read-only game-data lookup to an empty folder. Game data is read through application_retail_root() / retail_root.
HOME_RE = re.compile(r"(?<![\w.>])(?:\w+::)?(home|application_home)\(")
WRAP_RE = re.compile(r"(?<![\w.>])(?:\w+::)*(" + "|".join(WRAPPERS) + r")\s*\(")

def normalize(line):
    return " ".join(line.strip().split())

# A call is followed to its closing parenthesis over at most this many further lines; beyond that it is INCOMPLETE.
MAX_JOIN = 100

# (argument counts the function takes, index of its mode or flags argument).
FOPEN_MODE = {"fopen": ((2,), 1), "_wfopen": ((2,), 1), "fopen_s": ((3,), 2), "_wfopen_s": ((3,), 2),
              "freopen": ((3,), 1), "_wfreopen": ((3,), 1), "freopen_s": ((4,), 2), "_wfreopen_s": ((4,), 2),
              "_fsopen": ((3,), 1), "_wfsopen": ((3,), 1), "fdopen": ((2,), 1), "_fdopen": ((2,), 1)}
DESC_FLAGS = {"_open": ((2, 3), 1), "_wopen": ((2, 3), 1), "open": ((2, 3), 1), "_sopen": ((3, 4), 1),
              "_wsopen": ((3, 4), 1), "_sopen_s": ((5,), 2), "_wsopen_s": ((5,), 2)}   # _creat, _wcreat, creat: always
READ_ONLY_DESC_FLAGS = {"_O_RDONLY", "O_RDONLY", "_O_BINARY", "O_BINARY", "_O_TEXT", "O_TEXT", "_O_NOINHERIT",
                        "O_NOINHERIT", "O_CLOEXEC", "O_DIRECTORY", "_O_SEQUENTIAL", "_O_RANDOM"}
READ_ONLY_IOS = re.compile(r"(?:std::)?ios(?:_base)?::(in|binary)")
MEMBER_OPEN_ARGS = (2, 3)            # (path, mode) and MSVC's (path, mode, protection)

def call_span(code, start):
    """The index of the ')' closing the '(' at code[start], or -1. String and char contents in code are blank."""
    depth = 0
    for i in range(start, len(code)):
        c = code[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1

def split_args(code, text, start, end):
    """The call's top-level arguments, split on code (contents blank) and taken from text (column aligned)."""
    parts, depth, cur = [], 0, start + 1
    for i in range(start + 1, end):
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(text[cur:i].strip())
            cur = i + 1
    last = text[cur:end].strip()
    return parts + [last] if parts or last else []

def argument(args, shape):
    counts, index = shape
    return args[index] if len(args) in counts else None

# A flags or mode argument is exempt only when it is names joined by single bitwise ORs, with optional parentheses
# (2b.3 audit review: splitting on "|" alone read `_O_RDONLY || _O_BINARY`, which is 1, _O_WRONLY, as read only).
NAME = r"\s*\(*\s*(?:::)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*\s*\)*\s*"
BITWISE_OR_OF_NAMES = re.compile(NAME + r"(?:\|" + NAME + r")*")

def tokens(arg):
    """The names of a bitwise OR of names, or [] for anything else (a candidate)."""
    if not BITWISE_OR_OF_NAMES.fullmatch(arg):
        return []
    return [t for t in re.split(r"[\s|()]+", arg) if t]

def read_only_open(name, args):
    mode = argument(args, FOPEN_MODE[name])
    lit = re.fullmatch(r'L?"([^"]*)"', mode or "")
    if not lit:
        return False                      # a variable, computed or misplaced mode: a candidate
    flags = lit.group(1)
    return "w" not in flags and "a" not in flags and "+" not in flags and flags.startswith("r")

def read_only_desc(name, args):
    if name not in DESC_FLAGS:
        return False                      # _creat, _wcreat, creat always create
    flags = argument(args, DESC_FLAGS[name])
    t = [x[2:] if x.startswith("::") else x for x in tokens(flags or "")]
    return bool(t) and all(x in READ_ONLY_DESC_FLAGS for x in t) and any(x.endswith("O_RDONLY") for x in t)

def read_only_member_open(args):
    mode = args[1] if len(args) in MEMBER_OPEN_ARGS else None
    t = [READ_ONLY_IOS.fullmatch(x) for x in tokens(mode or "")]
    return bool(t) and all(t) and any(x.group(1) == "in" for x in t)

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

def preprocess(text):
    """A whole-file C++ lexer (2b fence review, nene 10191: a "/*" inside a string literal must not start a comment).
    Per line: (the line with comments removed and literals kept, the same with string and char contents blanked).
    Handles // and /* */ comments, "strings" with escapes, 'chars', raw strings R"d(...)d" with any prefix, and digit
    separators (1'000). Line structure is preserved."""
    nocomment, code = [], []
    cur_nc, cur_code = [], []
    i, n = 0, len(text)
    state, raw_end = "code", ""
    def newline():
        nocomment.append("".join(cur_nc).rstrip("\r"))
        code.append("".join(cur_code).rstrip("\r"))
        cur_nc.clear(); cur_code.clear()
    while i < n:
        c = text[i]
        if c == "\n":
            if state == "line":
                state = "code"
            elif state in ("string", "char"):
                state = "code"                          # an unterminated literal ends at the line (as the compiler would fail)
            newline()
            i += 1
            continue
        if c == "\r":                                   # kept in both, so a CRLF line inside a literal stays aligned
            cur_nc.append(c); cur_code.append(c); i += 1; continue
        if state == "code":
            if text.startswith("//", i):
                state = "line"; i += 2; continue
            if text.startswith("/*", i):
                state = "block"; i += 2
                cur_nc.append(" "); cur_code.append(" ")
                continue
            m = re.match(r'(?:u8|[uUL])?R"([^()\\ \t\n]{0,16})\(', text[i:i + 24]) if c in "uULR" else None
            if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
                state, raw_end = "raw", ")" + m.group(1) + '"'
                tok = m.group(0)
                cur_nc.append(tok); cur_code.append('"' + " " * (len(tok) - 1))
                i += len(tok); continue
            if c == '"':
                state = "string"; cur_nc.append(c); cur_code.append(c); i += 1; continue
            if c == "'":
                # A digit separator: inside a numeric literal (the token before it starts with a digit).
                j = i - 1
                while j >= 0 and (text[j].isalnum() or text[j] in "_.'"):
                    j -= 1
                token = text[j + 1:i]
                if token and token[0].isdigit():
                    cur_nc.append(c); cur_code.append(" "); i += 1; continue
                state = "char"; cur_nc.append(c); cur_code.append(c); i += 1; continue
            cur_nc.append(c); cur_code.append(c); i += 1; continue
        if state == "line":
            i += 1; continue
        if state == "block":
            if text.startswith("*/", i):
                state = "code"; i += 2; continue
            i += 1; continue
        if state in ("string", "char"):
            quote = '"' if state == "string" else "'"
            if c == "\\" and i + 1 < n and text[i + 1] != "\n":
                cur_nc.append(text[i:i + 2]); cur_code.append("  "); i += 2; continue
            if c == quote:
                state = "code"; cur_nc.append(c); cur_code.append(c); i += 1; continue
            cur_nc.append(c); cur_code.append(" "); i += 1; continue
        if state == "raw":
            if text.startswith(raw_end, i):
                state = "code"; cur_nc.append(raw_end); cur_code.append(" " * (len(raw_end) - 1) + '"'); i += len(raw_end); continue
            cur_nc.append(c); cur_code.append(" "); i += 1; continue
    newline()
    if text.endswith("\n"):
        nocomment.pop(); code.pop()
    return list(zip(nocomment, code))

def scan_text(rel, text):
    pre = preprocess(text)
    raws = [l.rstrip("\r") for l in text.split("\n")][:len(pre)]    # the same line breaks as the lexer
    # The whole file as one string twice, column aligned: comments removed (for argument text) and with string and char
    # contents blank as well (for parentheses and commas).
    flat = "\n".join(p[0] for p in pre)
    code_all = "\n".join(p[1] for p in pre)
    if len(flat) != len(code_all) or any(len(a) != len(b) for a, b in pre):
        raise SystemExit(f"write audit: the lexer lost column alignment in {rel}")
    starts, off = [], 0
    for _, code in pre:
        starts.append(off)
        off += len(code) + 1
    findings = []
    for i, (line, code) in enumerate(pre):
        if not code.strip() or code.lstrip().startswith("#"):
            continue
        kinds, incomplete = [], False
        # The line's own parentheses (a stream constructed on this line takes its path on the next).
        j, depth = i, code.count("(") - code.count(")")
        while depth > 0 and j + 1 < len(pre) and j - i < MAX_JOIN:
            j += 1
            depth += pre[j][1].count("(") - pre[j][1].count(")")
        if depth > 0:
            incomplete = True
        end = j
        def call(m):
            """Follow the call opened by m's '(' to its close; its arguments, or None when it does not close."""
            nonlocal end, incomplete
            at = starts[i] + m.end() - 1
            close = call_span(code_all, at)
            last = bisect.bisect_right(starts, close) - 1 if close >= 0 else -1
            if close < 0 or last - i > MAX_JOIN:
                incomplete = True
                return None
            end = max(end, last)
            return split_args(code_all, flat, at, close)
        for m in OPEN_RE.finditer(code):
            args = call(m)
            if args is None or not read_only_open(m.group(1), args):
                kinds.append(m.group(1))
        for m in DESC_RE.finditer(code):
            args = call(m)
            if args is None or not read_only_desc(m.group(1), args):
                kinds.append(m.group(1))
        for m in MEMBER_OPEN_RE.finditer(code):
            args = call(m)
            if args is None or not read_only_member_open(args):
                kinds.append(".open")
        if STREAM_RE.search(code):
            kinds.append(STREAM_RE.search(code).group(1))
        for pattern, name in ((WIN_RE, lambda m: m.group(1)), (C_RE, lambda m: m.group(1)),
                              (FS_RE, lambda m: m.group(1) or m.group(2)), (WRAP_RE, lambda m: "wrapper:" + m.group(1)),
                              (HOME_RE, lambda m: "home:" + m.group(1))):
            for m in pattern.finditer(code):
                call(m)
                kinds.append(name(m))
        if kinds:
            key = normalize(" ".join(raws[k] for k in range(i, end + 1)))
            finding = {"file": rel, "line": key, "number": i + 1, "kinds": sorted(set(kinds))}
            if incomplete:
                finding["incomplete"] = True
            findings.append(finding)
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
            text = p.read_bytes().decode("utf-8", errors="replace")
            for m in STANDALONE_INCLUDE.finditer(text):   # never classified: always an UNCLASSIFIED finding
                out.append(dict(file=rel, number=text.count("\n", 0, m.start()) + 1, kinds=["standalone include"],
                                line=m.group(0).strip()))
            out += scan_text(rel, text)
    return out

def audit(findings, entries):
    errors = []
    budget = {}
    for e in entries:
        key = (e["file"], normalize(e["line"]))
        budget[key] = budget.get(key, 0) + int(e.get("count", 1))
    used = {}
    for f in findings:
        if f.get("incomplete"):
            errors.append(f"INCOMPLETE {f['file']}:{f['number']} [{', '.join(f['kinds'])}] a call that does not close "
                          f"within {MAX_JOIN} lines cannot be classified: {f['line'][:160]}")
            continue
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
    ("xbox/source/platform_api.cpp", "if(const auto& run=outrun::xbox_runtime::latched_home();!run.empty()){", "if(!writable(retail_root))"),
    ("xbox/source/platform_api.cpp", "if(!same_folder(retail_root,outrun::xbox_runtime::latched_game_root()))throw std::runtime_error(", "return run;"),
    ("xbox/source/main.cpp", "if(const auto& run=outrun::xbox_runtime::latched_home();!run.empty())rt::home()=run;", "std::set_terminate("),
    ("src/system/setup_screen.cpp", "if(exe_image_load_cache(cache,&e,!game_dir_read_only))return true;", None),
    ("src/system/exe_image.cpp", "if(rewrite_in_place){std::string ignored;exe_image_write_cache(path,image,&ignored);}", None),
    ("windows/source/window_desktop.cpp", "outrun::xbox_runtime::latched_game_root()=folder;", "std::freopen(log.c_str(),\"w\",stdout)"),
    ("windows/source/test_fence.cpp", "if(plan.dbce_folder.empty()||has_reparse_point_on_path(plan.dbce_folder))return false;", None),
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
    # The 2b audit review's reproductions (artifacts/outrun-fence-audit-review-efa963c).
    cases.append(("fopen_s with wb", mutated(rank, lambda t: t + '\nstatic void fs(){std::FILE* f=nullptr;fopen_s(&f,"x.dat","wb");}\n')))
    cases.append(("_wfopen_s with wb", mutated(rank, lambda t: t + '\nstatic void wfs(){std::FILE* f=nullptr;_wfopen_s(&f,L"x.dat",L"wb");}\n')))
    cases.append(("_open with write flags", mutated(rank, lambda t: t + '\nstatic void op(const char* p){_open(p,_O_WRONLY|_O_CREAT|_O_TRUNC);}\n')))
    cases.append(("a stream opened with a variable mode", mutated(rank, lambda t: t + '\ntemplate<class S> void so(S& stream,const char* path){auto mode=std::ios::out|std::ios::trunc;stream.open(path,mode);}\n')))
    cases.append(("a writer after a \"/*\" string literal", mutated(rank, lambda t: t + '\nstatic const char* g="/*";\nstatic void w2(){std::FILE* f=std::fopen("x","wb");}\n')))
    cases.append(("a writer after a quote char literal", mutated(rank, lambda t: t + "\nstatic const char q='\"';\nstatic void w3(){std::FILE* f=std::fopen(\"y\",\"wb\");}\n")))
    cases.append(("a writer after a raw string holding /*", mutated(rank, lambda t: t + '\nstatic const char* r=R"(/* not a comment)";\nstatic void w4(){std::FILE* f=std::fopen("z","wb");}\n')))
    # The 2b.2 audit review's reproductions (artifacts/outrun-fence-review-26e8d47): only the real flags argument counts.
    cases.append(("_open with O_RDONLY OR an unknown mode", mutated(rank, lambda t: t + '\nstatic void d1(const char* p,int mode){_open(p,_O_RDONLY|mode);}\n')))
    cases.append(("a stream open with ios::in OR an unknown mode", mutated(rank, lambda t: t + '\ntemplate<class S,class M> void d2(S& s,const char* p,M mode){s.open(p,std::ios::in|mode);}\n')))
    cases.append(("O_RDONLY in the filename position", mutated(rank, lambda t: t + '\nstatic void d3(int mode){_open("O_RDONLY",mode);}\n')))
    cases.append(("_sopen_s with O_RDONLY OR an unknown mode", mutated(rank, lambda t: t + '\nstatic void d4(const char* p,int mode){int fh=-1;_sopen_s(&fh,p,_O_RDONLY|mode,_SH_DENYNO,0);}\n')))
    cases.append(("a read-only mode literal in the filename position", mutated(rank, lambda t: t + '\nstatic void d5(const char* m){std::fopen("rb",m);}\n')))
    # The 2b.3 audit review (artifacts/outrun-fence-review-1a2d2e9): a logical OR is not a bitwise one.
    cases.append(("_open with a logical OR of read-only flags", mutated(rank, lambda t: t + '\nstatic void d7(const char* p){_open(p,_O_RDONLY || _O_BINARY);}\n')))
    cases.append(("a stream open with a logical OR", mutated(rank, lambda t: t + '\nstatic void d8(std::fstream& s,const char* p){s.open(p,std::ios::in||std::ios::binary);}\n')))
    cases.append(("_open with flags in a conditional", mutated(rank, lambda t: t + '\nstatic void d9(const char* p,bool w){_open(p,w?_O_RDONLY:_O_BINARY);}\n')))
    cases.append(("an fopen with an argument count it does not take", mutated(rank, lambda t: t + '\nstatic void d6(const char* p){std::fopen(p,"rb","wb");}\n')))
    cases.append(("a removed writer (stale entry)", mutated(rank, lambda t: t.replace('std::fopen((dir+"/rankings.dat").c_str(),"wb")', 'nullptr'))))
    failures = []
    if audit(base, entries) or check_guards(repo):
        failures.append("the unmodified tree does not pass")
    api = "xbox/source/platform_api.cpp"
    api_text = (repo / api).read_bytes().decode("utf-8")
    guard_cases = [
        ("the latched home guard removed", {api: api_text.replace("if(const auto& run=outrun::xbox_runtime::latched_home();!run.empty()){", "{")}),
        ("the data-root check removed", {api: api_text.replace("if(!same_folder(retail_root,outrun::xbox_runtime::latched_game_root()))throw std::runtime_error(", "(void)(")}),
        ("the cache rewrite made unconditional", {"src/system/exe_image.cpp": (repo / "src/system/exe_image.cpp").read_bytes().decode("utf-8").replace(
            "if(rewrite_in_place){std::string ignored;exe_image_write_cache(path,image,&ignored);}", "{std::string ignored;exe_image_write_cache(path,image,&ignored);}")}),
        ("the LAN guard removed", {"xbox/source/main.cpp": (repo / "xbox/source/main.cpp").read_bytes().decode("utf-8").replace(
            "if(outrun::xbox_runtime::latched_home().empty())outrun::platform::native_network_set_platform(", "outrun::platform::native_network_set_platform(")}),
    ]
    for name, texts in guard_cases:
        if not check_guards(repo, texts):
            failures.append("NOT CAUGHT: " + name)
    for name, findings in cases:
        if not audit(findings, entries):
            failures.append("NOT CAUGHT: " + name)
    # A multiline call: the destination on the continuation line is part of the key (2b audit review).
    multi = 'static void m(){\n    std::FILE* f=std::fopen(\n        (safe_run_path+"/x.dat").c_str(),"wb");\n}\n'
    approved = [{"file": "windows/source/x.cpp", "line": f["line"], "class": "redirected"} for f in scan_text("windows/source/x.cpp", multi)]
    if len(approved) != 1 or audit(scan_text("windows/source/x.cpp", multi), approved):
        failures.append("the multiline control does not pass unmodified")
    changed = multi.replace("safe_run_path", "original_game_path")
    if not audit(scan_text("windows/source/x.cpp", changed), approved):
        failures.append("NOT CAUGHT: a multiline call whose destination changed on the continuation line")
    # A call longer than any line bound: the key still spans the whole call (2b.2 audit review: an approved fopen whose
    # destination changes after 14 continuation lines).
    long_call = ('void f(){auto* p=std::fopen(\n' + "".join(f" // continuation {k}\n" for k in range(14)) +
                 ' safe_run_path.c_str(),"wb");}\n')
    approved = [{"file": "windows/source/x.cpp", "line": f["line"], "class": "redirected"} for f in scan_text("windows/source/x.cpp", long_call)]
    if len(approved) != 1 or audit(scan_text("windows/source/x.cpp", long_call), approved):
        failures.append("the long multiline control does not pass unmodified")
    if not audit(scan_text("windows/source/x.cpp", long_call.replace("safe_run_path", "original_game_path")), approved):
        failures.append("NOT CAUGHT: a call whose destination changed after 14 continuation lines")
    # A call opened after a ')' on the same line: the line's own parentheses balance, so only following the call itself
    # reaches its destination.
    after_close = 'static void m2(){\n    run([]{\n    }); auto* f=std::fopen(\n        safe_run_path.c_str(),"wb");\n}\n'
    approved = [{"file": "windows/source/x.cpp", "line": f["line"], "class": "redirected"} for f in scan_text("windows/source/x.cpp", after_close) if "fopen" in f["kinds"]]
    if len(approved) != 1 or audit([f for f in scan_text("windows/source/x.cpp", after_close) if "fopen" in f["kinds"]], approved):
        failures.append("the call-after-close control does not pass unmodified")
    if not audit([f for f in scan_text("windows/source/x.cpp", after_close.replace("safe_run_path", "original_game_path")) if "fopen" in f["kinds"]], approved):
        failures.append("NOT CAUGHT: a call opened after a closing parenthesis whose destination changed on the next line")
    # A call that does not close within MAX_JOIN lines, or at all, is INCOMPLETE even when an entry names its key.
    for name, text in (("a call beyond the join bound", 'void g(){auto* p=std::fopen(\n' + " x\n" * (MAX_JOIN + 5) + ' safe.c_str(),"wb");}\n'),
                       ("a call that never closes", 'void h(){auto* p=std::fopen(\n safe.c_str(),"wb";\n')):
        found = scan_text("windows/source/x.cpp", text)
        named = [{"file": "windows/source/x.cpp", "line": f["line"], "class": "redirected"} for f in found]
        if not found or not all(f.get("incomplete") for f in found) or not audit(found, named):
            failures.append("NOT CAUGHT: " + name + " (classified despite being incomplete)")
    # Read-only counterparts are not flagged.
    ro = scan_text("x.cpp", 'void r(const char* p,std::ifstream& s){std::fopen(p,"rb");std::fopen(p,"r");std::FILE* f=nullptr;fopen_s(&f,p,"rb");'
                            '_open(p,_O_RDONLY|_O_BINARY);s.open(p,std::ios::in|std::ios::binary);}\n'
                            'void r2(const char* p){std::FILE* f=std::fopen(\n    p,"rb");}\n'
                            'void r3(const char* p){int fh=-1;_sopen_s(&fh,p,_O_RDONLY|_O_BINARY,_SH_DENYWR,_S_IREAD);'
                            '_open(p,(_O_RDONLY|_O_BINARY));_wopen(L"O_WRONLY",_O_RDONLY);}\n'
                            'void r4(const char* p){std::FILE* f=_fsopen(p,"rb",_SH_DENYWR);std::freopen(p,"r",stdin);}\n')
    if ro:
        failures.append("a read-only open was flagged: " + "; ".join(f["line"] for f in ro))
    return failures, len(cases) + len(guard_cases) + 6

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
        print(f"write audit selftest: {n} mutations plus read-only controls, {len(failures)} failure(s)")
        return 1 if failures else 0
    errors = audit(scan_tree(repo), entries) + check_guards(repo)
    for e in errors:
        print(e)
    print(f"write audit: {len(entries)} classified entr{'y' if len(entries) == 1 else 'ies'}, {len(errors)} problem(s)")
    return 1 if errors else 0

if __name__ == "__main__":
    sys.exit(main())
