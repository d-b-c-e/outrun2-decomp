"""P21k: pin clean source, build (ClangCL), run the P21 host over the real Core and DInputProvider on fake COM in supervised
children, zero's own provider tests against the extended fake, and the P21g teardown audit with its mutations; optional host
mutations, each required to fail its intended check; write a receipt. No device, game, installation or launch."""
import argparse, hashlib, json, pathlib, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument('result', type=pathlib.Path)
ap.add_argument('--mutations', action='store_true')
args = ap.parse_args(); here = pathlib.Path(__file__).resolve().parent; repo = here.parents[2]
result = args.result.resolve()
if result.exists(): raise SystemExit('Use a new result directory.')
def git(*a): return subprocess.check_output(['git', '-C', str(repo), *a], text=True).strip()
def pin():
    if git('status', '--porcelain'): raise RuntimeError('Uncommitted or untracked source; commit first.')
    return dict(commit=git('rev-parse', 'HEAD'), sha256={p: hashlib.sha256((repo / p).read_bytes()).hexdigest()
                for p in git('ls-files', '--', 'windows/shim', 'windows/tests/shim_fake_com.hpp', 'windows/tests/test_dinput_provider.cpp',
                             'windows/tests/p21_host_spike/host.hpp', 'windows/tests/p21_provider_host', 'windows/tests/teardown_audit.py').splitlines()})
before = pin(); result.mkdir(parents=True); commands = []
def run(name, cmd, expected=0, cwd=repo):
    p = subprocess.run([str(x) for x in cmd], cwd=cwd, capture_output=True, text=True, timeout=600)
    (result / (name + '.log')).write_text(p.stdout + p.stderr)
    commands.append(dict(name=name, command=[str(x) for x in cmd], exit=p.returncode))
    if expected is not None and p.returncode != expected: raise RuntimeError(name + ' failed; see its log')
    return p
def build(source, out, prefix):
    run(prefix + '-configure', ['cmake', '-S', source, '-B', out, '-G', 'Visual Studio 17 2022', '-T', 'ClangCL', '-A', 'x64'])
    run(prefix + '-build', ['cmake', '--build', out, '--config', 'Release'])
    return out / 'Release'
bins = build(here, result / 'build', 'p21k')
proof = run('p21k', [bins / 'p21_provider_host.exe', result / 'runs'])
zero = run('zero-provider-tests', [bins / 'test_dinput_provider.exe'])
audit = run('teardown-audit', [sys.executable, '-I', repo / 'windows/tests/teardown_audit.py', repo])
audit_m = run('teardown-audit-mutations', [sys.executable, '-I', repo / 'windows/tests/teardown_audit.py', repo, '--mutations'])
mutations = []
if args.mutations:
    # Each breaks one rule of the host in a scratch copy and must make a named mode's check fail.
    plan = {
        'host-ignores-held-devices': ('        if (rep.held) r.unresolved.push_back("lease leaked: " + std::to_string(rep.held) + " device(s) still inside a call");\n',
                                      '', 'P21k never: an event-backed reacquisition'),
        'host-ignores-worker-join': ('        if (!join_worker_until(Clock::now() + std::chrono::milliseconds(b.workers_ms))) r.unresolved.push_back("worker provider");\n',
                                     '        (void)join_worker_until(Clock::now() + std::chrono::milliseconds(b.workers_ms));\n', 'P21k listing-never'),
        'host-ignores-directinput': ('        if (!rep.input_released) r.unresolved.push_back("DirectInput not released: a call may still use it");\n',
                                     '', 'P21k listing-never'),
    }
    for name, (old, new, reason) in plan.items():
        src = result / 'mutations' / name; shutil.copytree(here, src)
        h = (src / 'host.hpp').read_text(encoding='utf-8')
        if h.count(old) != 1: raise RuntimeError('Mutation anchor changed: ' + name)
        (src / 'host.hpp').write_text(h.replace(old, new), encoding='utf-8')
        # The copy's main.cpp includes "p21_provider_host/host.hpp": point that include at the mutated copy.
        m = (src / 'main.cpp').read_text(encoding='utf-8').replace('#include "p21_provider_host/host.hpp"', '#include "host.hpp"')
        (src / 'main.cpp').write_text(m, encoding='utf-8')
        c = (src / 'CMakeLists.txt').read_text(encoding='utf-8').replace('set(WIN ${CMAKE_CURRENT_SOURCE_DIR}/../..)', 'set(WIN ' + (repo / 'windows').as_posix() + ')')
        (src / 'CMakeLists.txt').write_text(c, encoding='utf-8')
        mb = build(src, src / 'build', name)
        p = run(name + '-run', [mb / 'p21_provider_host.exe', src / 'runs'], expected=None)
        caught = p.returncode != 0 and any(l.startswith('FAIL ' + reason) for l in p.stdout.splitlines())
        if not caught: raise RuntimeError('Mutation not caught by its intended check: ' + name)
        mutations.append(dict(name=name, detected=True, exit=p.returncode, failed=[l for l in p.stdout.splitlines() if l.startswith('FAIL')]))
after = pin()
if before != after: raise RuntimeError('Source changed during the proof.')
receipt = dict(sourceCommit=before['commit'], sourceVerified=True,
    scope='offline: the P21 host over zero\'s real Core and DInputProvider on fake COM objects, supervised children; no device, game, installation or launch',
    results=dict(p21k=proof.stdout.strip().splitlines()[-1], zeroProviderTests=zero.stdout.strip().splitlines()[-1],
                 teardownAudit=audit.stdout.strip().splitlines()[-1], teardownAuditMutations=[l for l in audit_m.stdout.splitlines() if l.startswith('mutation') or 'selftest' in l]),
    commands=commands, mutations=mutations, pins=before['sha256'])
(result / 'receipt.json').write_text(json.dumps(receipt, indent=2))
for k, v in receipt['results'].items(): print(k + ':', v if isinstance(v, str) else '; '.join(v))
print('Host mutations caught:', len(mutations), 'Receipt:', result / 'receipt.json')
