"""Offline saved-while-alive protocol tests, using explicitly synthesized @2 copies of a P21 fixture.
No game, input, device or install. Original fixture bytes are read-only; its native capture remains unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument('fixture', type=Path, help='existing resolved/capture fixture directory')
ap.add_argument('result', type=Path)
ap.add_argument('--dll', type=Path, required=True)
a = ap.parse_args()
a.result.mkdir(parents=True, exist_ok=False)
request, exe, pid, creation = '1' * 32, '2' * 64, 1234, 134362000000000000
members = sorted(['bundle.meta', 'profile.ini', 'worker.tsv', 'source.jsonl', 'budget.tsv', 'devices.json'])
checks = []

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def write_json(p, v):
    p.write_text(json.dumps(v, separators=(',', ':')) + '\n', encoding='utf-8', newline='\n')

def refresh_source(d, rows):
    prefix = ''.join(json.dumps(v, separators=(',', ':')) + '\n' for v in rows[:-1])
    rows[-1]['footer']['counts']['dataBytesWritten'] = len(prefix.encode())
    (d / 'source.jsonl').write_text(prefix + json.dumps(rows[-1], separators=(',', ':')) + '\n', encoding='utf-8', newline='\n')
    w = (d / 'worker.tsv').read_text().strip().split('\t')
    w[8], w[9], w[10] = rows[0]['metadata']['properties']['captureNonce'], str((d / 'source.jsonl').stat().st_size), sha(d / 'source.jsonl')
    (d / 'worker.tsv').write_text('\t'.join(w) + '\n', encoding='utf-8')

def completion(d):
    w = (d / 'worker.tsv').read_text().strip().split('\t')
    entries = [dict(name=n, bytes=(d / n).stat().st_size, sha256=sha(d / n)) for n in members]
    digest = ''.join(f"{e['name']}\t{e['bytes']}\t{e['sha256']}\n" for e in entries)
    report = dict(attempted=int(w[3]), accepted=int(w[1]), written=int(w[2]), declaredLast=int(w[4]), lastWritten=int(w[5]),
                  queueFull=0, invalid=0, afterFailure=0, afterEnd=0, wrongProducer=0, discarded=0,
                  firstRejected=False, captureCountsFinal=True, drainTimedOut=False, producerDone=True, workerDone=True)
    return dict(schema='outrun.recorder-complete@1', contract='dbce.shim.outrun-capture@2', requestId=request,
                outcome='Complete', joined=True, stoppedBy='f11', report=report, pushed=int(w[2]), missedFinal=0,
                budget=dict(hz=60, seconds=1, maxBytes=1024*1024, slots=2), profileSha256=sha(d/'profile.ini'), exeSha256=exe,
                processId=pid, processCreationFileTime=creation, startedUtc='2026-10-12T13:00:00Z', completedUtc='2026-10-12T13:00:01Z',
                members=entries, artifactSha256=hashlib.sha256(digest.encode()).hexdigest())

base = a.result/'synthetic-v2'
base.mkdir()
for n in ['bundle.meta', 'profile.ini', 'worker.tsv', 'source.jsonl', 'budget.tsv']:
    shutil.copyfile(a.fixture/n, base/n)
(base/'bundle.meta').write_text('dbce.shim.outrun-capture@2\n', encoding='utf-8', newline='\n')
write_json(base/'devices.json', [])
rows = [json.loads(s) for s in (base/'source.jsonl').read_text().splitlines()]
rows[0]['metadata']['game'] = 'outrun2006-decomp-desktop'
rows[0]['metadata']['properties'].update(runNonce=request, captureNonce='3'*32, context='synthetic protocol test, not runtime evidence')
final = ['final.'+n for n in 'steering accelerator brake shiftUp shiftDown menuLeft menuRight menuUp menuDown menuConfirm menuCancel start featureMask deviceHeld frontAxis0 frontAxis1 frontAxis2 frontAxis3 connected pcInputLayer pcRecordUsed pcOverlay pcOverlayYield pcButtons pcSteer pcAccel pcBrake'.split()]
final += ['keyboard.'+n for n in 'enter esc up down left right any drives'.split()]
for row in rows[1:-1]:
    row['sample']['channels'].update({n:0 for n in final})
names = rows[1]['sample']['channels'].keys()
bound = 192 + sum(len(json.dumps(n))+34 for n in names)
(base/'budget.tsv').write_text(f'{len(names)}\t{bound}\t16432\n', encoding='utf-8')
refresh_source(base, rows)
write_json(base/'recorder-complete.json', completion(base))

def copy(name):
    d = a.result/name
    shutil.copytree(base, d)
    return d

def command(d, name, expected=0, mode='--seal-recording', overrides=None):
    args = [request, exe, str(pid), str(creation)] if overrides is None else overrides
    receipt = a.result/(name+'.json')
    p = subprocess.run(['dotnet', str(a.dll), mode, str(d), *args, str(receipt)], capture_output=True, text=True, timeout=30)
    (a.result/(name+'.log')).write_text(p.stdout+p.stderr, encoding='utf-8')
    assert (p.returncode == 0) == (expected == 0), (name, p.stdout, p.stderr)
    assert receipt.exists() == (expected == 0), name
    checks.append(dict(name=name, refused=p.returncode!=0))
    return receipt

ok = copy('alive')
receipt = command(ok, 'alive-save')
assert not (ok/'complete.tsv').exists() and not (ok/'exit.json').exists()
assert json.loads(receipt.read_text())['recordingSaved']
command(ok, 'alive-read', mode='--read-recording')
command(ok, 'no-overwrite', expected=1)
command(ok, 'wrong-request', expected=1, mode='--read-recording', overrides=['4'*32, exe, str(pid), str(creation)])
command(ok, 'wrong-process', expected=1, mode='--read-recording', overrides=[request, exe, str(pid+1), str(creation)])
command(ok, 'wrong-executable', expected=1, mode='--read-recording', overrides=[request, '5'*64, str(pid), str(creation)])

changes = {
    'not-joined': lambda r: r.update(joined=False),
    'not-complete': lambda r: r.update(outcome='TimedOut'),
    'dropped': lambda r: r['report'].update(discarded=1),
    'after-end': lambda r: r['report'].update(afterEnd=1),
    'not-final': lambda r: r['report'].update(captureCountsFinal=False),
    'wrong-count': lambda r: r['report'].update(written=15),
    'missed-final': lambda r: r.update(missedFinal=1),
    'missing-member': lambda r: r['members'].pop(),
    'path-member': lambda r: r['members'][0].update(name='../bundle.meta'),
    'bad-artifact': lambda r: r.update(artifactSha256='0'*64),
    'backward-time': lambda r: r.update(completedUtc='2026-10-12T12:00:00Z'),
    'budget-too-small': lambda r: r['budget'].update(maxBytes=10),
    'wrong-stop': lambda r: r.update(stoppedBy='fault'),
    'zero-process': lambda r: r.update(processId=0),
}
for name, edit in changes.items():
    d=copy(name)
    r=json.loads((d/'recorder-complete.json').read_text()); edit(r); write_json(d/'recorder-complete.json',r)
    command(d,name,expected=1)
    assert not (d/'recording-complete.tsv').exists()
for name, member in [('missing-worker','worker.tsv'), ('missing-report','recorder-complete.json')]:
    d=copy(name); (d/member).unlink(); command(d,name,expected=1)
tampered = a.result/'tampered'
shutil.copytree(ok,tampered)
(tampered/'profile.ini').write_text('changed', encoding='utf-8')
command(tampered,'tampered',expected=1,mode='--read-recording')
for name, channel in [('missing-final-channel','final.steering'),('missing-keyboard-channel','keyboard.drives')]:
    d=copy(name)
    modified=[json.loads(s) for s in (d/'source.jsonl').read_text().splitlines()]
    for row in modified[1:-1]: del row['sample']['channels'][channel]
    names=modified[1]['sample']['channels'].keys()
    (d/'budget.tsv').write_text(f'{len(names)}\t{192+sum(len(json.dumps(n))+34 for n in names)}\t16432\n',encoding='utf-8')
    refresh_source(d,modified); write_json(d/'recorder-complete.json',completion(d))
    command(d,name,expected=1)

# Saving while alive does not bypass the process seal. It still needs the SAME host plus a normal external exit.
for name, forced, different in [('normal-exit',False,False),('forced-exit',True,False),('other-process',False,True)]:
    d=a.result/name; shutil.copytree(ok,d)
    write_json(d/'host-shutdown.json',dict(state='resolved',nonce=request,recorderJoined=True,recorderOutcome=2,
        unresolved=[],pid=pid+(1 if different else 0),creationTime=creation))
    write_json(d/'exit.json',dict(pid=pid+(1 if different else 0),creationTime=creation,forced=forced,exitCode=0))
    p=subprocess.run(['dotnet',str(a.dll),'--seal-export',str(d),str(a.result/(name+'.txt'))],capture_output=True,text=True,timeout=30)
    (a.result/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
    assert (p.returncode==0)==(not forced and not different),(name,p.stdout,p.stderr)
    checks.append(dict(name=name,refused=p.returncode!=0))
write_json(a.result/'results.json',dict(passed=True,scope='synthetic protocol only; no runtime or gameplay claim',checks=checks))
print(f'PASS {len(checks)} recording completion scenarios')
