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
for i, row in enumerate(rows[1:-1]):
    row['sample']['channels'].update({n:0 for n in final})
    # Additive inputs must survive export without changing the required @2 schema.
    row['sample']['channels'].update({'final.pcDrive':i%2, 'final.pcAxis01':0.125,
        'final.pcAxis03':-0.3141592653589793, 'final.pcAxis17':float(i), 'final.pcAxis18':1e-12})
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

def export(d, name, expected=0, overrides=None, output=None):
    args=[request, exe, str(pid), str(creation)] if overrides is None else overrides
    output=a.result/(name+'.txt') if output is None else output
    p=subprocess.run(['dotnet',str(a.dll),'--export-recording',str(d),*args,str(output)],
                     capture_output=True,text=True,timeout=30)
    (a.result/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
    assert (p.returncode==0)==(expected==0),(name,p.stdout,p.stderr)
    checks.append(dict(name=name,refused=p.returncode!=0))
    return output

before={p.name:sha(p) for p in ok.iterdir()}
exported=export(ok,'alive-export')
assert not (ok/'complete.tsv').exists() and not (ok/'host-shutdown.json').exists() and not (ok/'exit.json').exists()
lines=iter(exported.read_text(encoding='utf-8-sig').splitlines())
for row in rows[1:-1]:
    channels=row['sample']['channels']
    assert next(lines)==f'ROW {len(channels)}'
    for key in sorted(channels):
        k,v=next(lines).split(' ')
        assert k==key and float(v)==float(channels[key]),(k,v,channels[key])
assert next(lines,None) is None
assert before=={p.name:sha(p) for p in ok.iterdir()}
export(ok,'export-wrong-request',1,['4'*32,exe,str(pid),str(creation)])
export(ok,'export-wrong-exe',1,[request,'5'*64,str(pid),str(creation)])
export(ok,'export-wrong-pid',1,[request,exe,str(pid+1),str(creation)])
export(ok,'export-wrong-creation',1,[request,exe,str(pid),str(creation+1)])
for n in ['export-wrong-request','export-wrong-exe','export-wrong-pid','export-wrong-creation']:
    assert not (a.result/(n+'.txt')).exists()
export(ok,'export-no-overwrite',1,output=exported)
assert before=={p.name:sha(p) for p in ok.iterdir()}
inside=ok/'export.txt'
export(ok,'export-inside-capture',1,output=inside)
assert not inside.exists()
unsealed=copy('unsealed-export')
export(unsealed,'export-missing-seal',1)
assert not (a.result/'export-missing-seal.txt').exists()

changes = {
    'record-tape-end': lambda r: r.update(stoppedBy='tape-end', action='record'),
    'legacy-tape-end': lambda r: r.update(stoppedBy='tape-end'),
    'unknown-action': lambda r: r.update(action='unknown'),
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
replay=copy('playback-tape-end')
replay_report=json.loads((replay/'recorder-complete.json').read_text())
replay_report.update(action='playback',stoppedBy='tape-end')
write_json(replay/'recorder-complete.json',replay_report)
command(replay,'playback-tape-end')
command(replay,'playback-tape-end-read',mode='--read-recording')
for name, member in [('missing-worker','worker.tsv'), ('missing-report','recorder-complete.json')]:
    d=copy(name); (d/member).unlink(); command(d,name,expected=1)
tampered = a.result/'tampered'
shutil.copytree(ok,tampered)
(tampered/'profile.ini').write_text('changed', encoding='utf-8')
command(tampered,'tampered',expected=1,mode='--read-recording')
export(tampered,'export-tampered',1)
assert not (a.result/'export-tampered.txt').exists()
for name, channel in [('missing-final-channel','final.steering'),('missing-keyboard-channel','keyboard.drives')]:
    d=copy(name)
    modified=[json.loads(s) for s in (d/'source.jsonl').read_text().splitlines()]
    for row in modified[1:-1]: del row['sample']['channels'][channel]
    names=modified[1]['sample']['channels'].keys()
    (d/'budget.tsv').write_text(f'{len(names)}\t{192+sum(len(json.dumps(n))+34 for n in names)}\t16432\n',encoding='utf-8')
    refresh_source(d,modified); write_json(d/'recorder-complete.json',completion(d))
    command(d,name,expected=1)
    # A byte-correct digest cannot make an invalid recording valid through the process-level API.
    sealed = sorted(members+['recorder-complete.json'])
    (d/'recording-complete.tsv').write_text('dbce-playback-seal-v1\n'+''.join(f'{n}\t{(d/n).stat().st_size}\t{sha(d/n)}\n' for n in sealed),encoding='utf-8',newline='\n')
    export(d,name+'-export',1)
    assert not (a.result/(name+'-export.txt')).exists()
    write_json(d/'host-shutdown.json',dict(state='resolved',nonce=request,recorderJoined=True,recorderOutcome=2,
        unresolved=[],pid=pid,creationTime=creation))
    write_json(d/'exit.json',dict(pid=pid,creationTime=creation,forced=False,exitCode=0))
    process_members = sorted(sealed+['recording-complete.tsv','host-shutdown.json','exit.json'])
    for mode in ['--seal-export','--read-export']:
        if mode=='--read-export':
            (d/'complete.tsv').write_text('dbce-playback-seal-v1\n'+''.join(f'{n}\t{(d/n).stat().st_size}\t{sha(d/n)}\n' for n in process_members),encoding='utf-8',newline='\n')
        p=subprocess.run(['dotnet',str(a.dll),mode,str(d),str(a.result/(name+mode+'.txt'))],capture_output=True,text=True,timeout=30)
        (a.result/(name+mode+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        assert p.returncode!=0 and 'complete post-arbitration input channels' in p.stderr,(name,mode,p.stdout,p.stderr)
        checks.append(dict(name=name+mode,refused=True))

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
    if name=='normal-exit':
        assert (a.result/(name+'.txt')).read_bytes()==exported.read_bytes(), 'ordinary and P21 export formats differ'
# Product comparison holds and verifies both ordinary seals before reading their observations.
# These small, explicitly synthetic copies exercise the production command, not game behavior.
def posed_copy(name, is_replay, change=None, report_change=None):
    d=copy(name)
    values=[json.loads(s) for s in (d/'source.jsonl').read_text().splitlines()]
    for i, row in enumerate(values[1:-1]):
        c=row['sample']['channels']
        c.update({'gameInput.mode':16,'gameInput.gear':2,'gameInput.speed':40.0,
            'gameInput.consumedSteer':0,'gameInput.consumedAccel':100,'gameInput.consumedBrake':0,
            'gameInput.poseValid':1,'gameInput.posX':float(i),'gameInput.posY':0.0,'gameInput.posZ':float(i)*2,
            'gameInput.vec20X':0,'gameInput.vec20Y':0,'gameInput.vec20Z':1,
            'gameInput.carModel':1,'gameInput.stageKey':0,'gameInput.courseGate':0,
            'replay.tapeRow':i if is_replay else -1})
        c.update({'gameInput.rot%d%d'%(r,k):int(r==k) for r in range(3) for k in range(3)})
    if change: change(values)
    names=values[1]['sample']['channels'].keys()
    (d/'budget.tsv').write_text(f'{len(names)}\t{192+sum(len(json.dumps(n))+34 for n in names)}\t16432\n',encoding='utf-8')
    refresh_source(d,values)
    report=completion(d)
    if is_replay: report.update(action='playback',stoppedBy='tape-end',sourceSealSha256=sha(posed_ref/'recording-complete.tsv'))
    if report_change: report_change(report)
    write_json(d/'recorder-complete.json',report)
    command(d,name+'-seal',overrides=[request,report['exeSha256'],str(pid),str(creation)])
    return d

def compare(ref, rep, name, expected, artifact=True):
    output=a.result/(name+'.json')
    p=subprocess.run(['dotnet',str(a.dll),'--compare-recordings',str(ref),str(rep),str(output)],
                     capture_output=True,text=True,timeout=30)
    (a.result/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
    assert p.returncode==expected,(name,p.stdout,p.stderr)
    assert output.exists()==artifact,name
    if artifact:
        value=json.loads(output.read_text())
        assert value['sealed'] and value['referenceSealSha256']==sha(ref/'recording-complete.tsv')
        assert value['replaySealSha256']==sha(rep/'recording-complete.tsv')
        assert value['valid']==(expected!=1)
        if expected==0: assert value['followed'] and value['pose']['pairedRows']==len(rows)-2
    checks.append(dict(name=name,exitCode=p.returncode))

posed_ref=posed_copy('posed-reference',False)
posed_replay=posed_copy('posed-replay',True)
compare(posed_ref,posed_replay,'posed-comparison',0)
admitted=command(posed_ref,'posed-admission',mode='--read-playback-recording')
assert json.loads(admitted.read_text())['playbackEligible']
command(ok,'pose-free-admission',expected=1,mode='--read-playback-recording')
command(posed_replay,'replay-as-reference-admission',expected=1,mode='--read-playback-recording')
playback_export=command(posed_ref,'posed-playback-export',mode='--export-playback')
assert playback_export.read_text(encoding='utf-8-sig').startswith('ROW ')
command(ok,'pose-free-playback-export',expected=1,mode='--export-playback')
different=posed_copy('posed-divergence',True,lambda v:v[3]['sample']['channels'].update({'gameInput.posX':99}))
compare(posed_ref,different,'posed-divergence-comparison',2)
different_input=posed_copy('posed-input-difference',True,lambda v:v[3]['sample']['channels'].update({'final.steering':99}))
compare(posed_ref,different_input,'posed-input-difference-comparison',2)
other_exe=posed_copy('posed-other-exe',True,report_change=lambda r:r.update(exeSha256='6'*64))
compare(posed_ref,other_exe,'posed-other-exe-comparison',1,False)
other_source=posed_copy('posed-other-source',True,report_change=lambda r:r.update(sourceSealSha256='7'*64))
compare(posed_ref,other_source,'posed-other-source-comparison',1,False)
compare(posed_ref,posed_ref,'ordinary-not-replay-comparison',1,False)
compare(posed_replay,posed_replay,'replay-not-reference-comparison',1,False)
input_only_replay=posed_copy('input-only-source-replay',True,report_change=lambda r:r.update(sourceSealSha256=sha(ok/'recording-complete.tsv')))
compare(ok,input_only_replay,'input-only-not-product-comparison',1)
tampered_replay=a.result/'posed-tampered'
shutil.copytree(posed_replay,tampered_replay)
(tampered_replay/'source.jsonl').write_text('changed',encoding='utf-8')
compare(posed_ref,tampered_replay,'posed-tampered-comparison',1,False)
write_json(a.result/'results.json',dict(passed=True,scope='synthetic protocol only; no runtime or gameplay claim',checks=checks))
print(f'PASS {len(checks)} recording completion scenarios')
