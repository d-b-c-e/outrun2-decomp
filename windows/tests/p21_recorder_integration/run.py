"""P21 host + reviewed P20 recorder: pin clean source and toolkit, build, run supervised children (actual Core/Adapter
frames, the real native session writer), then the out-of-process finalizer; optional mutations; write a receipt."""
import argparse, hashlib, json, pathlib, shutil, subprocess

ap = argparse.ArgumentParser()
ap.add_argument('result', type=pathlib.Path)
ap.add_argument('--toolkit', type=pathlib.Path, required=True)
ap.add_argument('--mutations', action='store_true')
args = ap.parse_args(); here = pathlib.Path(__file__).resolve().parent; repo = here.parents[2]
result = args.result.resolve(); toolkit = args.toolkit.resolve()
if result.exists(): raise SystemExit('Use a new result directory.')
def git(root, *a): return subprocess.check_output(['git', '-C', str(root), *a], text=True).strip()
def pin(root, scopes):
    if git(root, 'status', '--porcelain', '--', *scopes): raise RuntimeError('Uncommitted source: ' + str(root))
    return dict(commit=git(root, 'rev-parse', 'HEAD'), sha256={p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in git(root, 'ls-files', '--', *scopes).splitlines()})
tk_scopes = ['native/session', 'dotnet/Dbce.Wheel.Recording', 'dotnet/Dbce.Wheel.Playback/ArtifactSeal.cs', 'Directory.*', 'global.json', 'NuGet.Config']
before = dict(source=pin(repo, ['.']), toolkit=pin(toolkit, tk_scopes))
# A clean Windows worktree can materialize the same reviewed source as CRLF.
# Pin every actual file byte above/below; this source-admission comparison ignores only line endings.
def writer_hash(path): return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
if writer_hash(toolkit / 'native/session/session_writer.h') != 'a9de5d49c55b9b4be464fb173178d20a97972eacff500aee0267e30fb44d19db' or \
   writer_hash(repo / 'windows/vendor/session/session_writer.h') != 'a9de5d49c55b9b4be464fb173178d20a97972eacff500aee0267e30fb44d19db':
    raise RuntimeError('Unreviewed native writer.')
result.mkdir(parents=True); (result / 'source-before.json').write_text(json.dumps(before, indent=2)); commands = []
def run(name, cmd, expected=0):
    p = subprocess.run([str(x) for x in cmd], cwd=repo, capture_output=True, text=True, timeout=300)
    (result / (name + '.log')).write_text(p.stdout + p.stderr)
    commands.append(dict(name=name, command=[str(x) for x in cmd], exit=p.returncode))
    if expected is not None and p.returncode != expected: raise RuntimeError(name + ' failed; see its log')
    return p
def native_build(source, build, prefix):
    run(prefix + '-configure', ['cmake', '-S', source, '-B', build, '-G', 'Visual Studio 17 2022', '-T', 'ClangCL', '-A', 'x64',
        '-DTOOLKIT_ROOT=' + str(toolkit), '-DSHIM_SOURCE_ROOT=' + str(repo)])
    run(prefix + '-build', ['cmake', '--build', build, '--config', 'Debug'])
    return build / 'Debug/p21_recorder_integration.exe'
def managed_build(project, artifacts, prefix):
    run(prefix + '-managed-build', ['dotnet', 'build', project, '-c', 'Release', '--artifacts-path', artifacts, '-p:ToolkitRoot=' + str(toolkit)])
    return artifacts / 'bin/P21Finalizer/release/P21Finalizer.dll'
exe = native_build(here, result / 'native', 'native')
dll = managed_build(here / 'managed/P21Finalizer.csproj', result / 'managed', 'finalizer')
runs = result / 'runs'
run('supervised', [exe, runs])
pristine = result / 'runs-pristine'; shutil.copytree(runs, pristine)   # captures exactly as the supervisor left them
run('finalize', ['dotnet', dll, runs])
mutations = []
if args.mutations:
    native = {
        'child-returns-instead-of-parking': [('if (!r.resolved) { if (latched) host->park_for_supervisor(); host->forced_exit(); }',
                                              'if (!r.resolved && !latched) host->forced_exit();')],
        'host-ignores-provider-and-lease': [('if (leases_.leaked) r.unresolved.push_back("lease leaked");', '(void)leases_.leaked;'),
                                            ('if (!w->join_until(workers)) r.unresolved.push_back("worker " + w->name());', '(void)w->join_until(workers);')],
    }
    reasons = {'child-returns-instead-of-parking': 'a writer that never returns: unresolved, parked, ended only by the supervisor',
               'host-ignores-provider-and-lease': 'a complete recorder beside a stuck provider: unresolved, parked, ended by the supervisor'}
    for name, edits in native.items():
        src = result / 'mutations' / name; src.mkdir(parents=True)
        for f in ['CMakeLists.txt', 'integration.cpp']: shutil.copyfile(here / f, src / f)
        s = (src / 'integration.cpp').read_text()
        for old, new in edits:
            if s.count(old) != 1: raise RuntimeError('Mutation anchor changed: ' + name)
            s = s.replace(old, new)
        (src / 'integration.cpp').write_text(s)
        mx = native_build(src, src / 'build', name)
        p = run(name + '-supervised', [mx, src / 'runs'], expected=None)
        if p.returncode == 0 or reasons[name] not in p.stderr: raise RuntimeError('Mutation not caught by intended check: ' + name)
        mutations.append(dict(name=name, detected=True, stage='supervised', exit=p.returncode, output=p.stdout + p.stderr))
    managed = {
        'finalizer-ignores-host-state': ('Check(Str(h,"state")=="resolved","host shutdown resolved");', '',
                                         'a valid worker report beside an unresolved host and a forced exit never seals'),
        'finalizer-ignores-exit-observation': ('Check(!Bool(e,"forced")&&Num(e,"exitCode")==0,"normal exit observed, not forced");', '',
                                               'forced exit refused'),
        'finalizer-accepts-zero-identity': ('Check(Num(h,"pid")!=0&&Num(h,"creationTime")!=0&&Num(e,"pid")!=0&&Num(e,"creationTime")!=0,"nonzero process identity in both reports");', '',
                                            'matching zero identities refused'),
    }
    for name, (old, new, reason) in managed.items():
        src = result / 'mutations' / name; src.mkdir(parents=True)
        for f in ['P21Finalizer.csproj', 'Program.cs', 'RecordingCompletion.cs']: shutil.copyfile(here / 'managed' / f, src / f)
        s = (src / 'Program.cs').read_text()
        if s.count(old) != 1: raise RuntimeError('Mutation anchor changed: ' + name)
        (src / 'Program.cs').write_text(s.replace(old, new))
        mdll = managed_build(src / 'P21Finalizer.csproj', src / 'artifacts', name)
        captures = src / 'runs'; shutil.copytree(pristine, captures)
        p = run(name + '-finalize', ['dotnet', mdll, captures], expected=None)
        if p.returncode == 0 or reason not in p.stderr: raise RuntimeError('Mutation not caught by intended check: ' + name)
        mutations.append(dict(name=name, detected=True, stage='finalize', exit=p.returncode, output=p.stdout + p.stderr))
after = dict(source=pin(repo, ['.']), toolkit=pin(toolkit, tk_scopes))
if before != after: raise RuntimeError('Source changed during proof.')
evidence = {str(p.relative_to(result)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (result / 'runs').rglob('*') if p.is_file()}
receipt = dict(sourceCommit=before['source']['commit'], toolkitCommit=before['toolkit']['commit'], sourceVerified=True,
    scope='offline: P21 host + reviewed P20 worker + real native writer + actual Core/Adapter frames from fake devices; supervised child processes; no device, game, installation or launch',
    commands=commands, mutations=mutations, sha256=evidence)
(result / 'receipt.json').write_text(json.dumps(receipt, indent=2))
for name in ['supervised', 'finalize']: print((result / (name + '.log')).read_text().strip().splitlines()[-1])
print('Mutations detected:', len(mutations), 'Receipt:', result / 'receipt.json')
