"""Offline atomic transport proof and synchronization negative controls; no game or devices."""
import argparse, hashlib, json, pathlib, shutil, subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('result',type=pathlib.Path)
args=p.parse_args();here=pathlib.Path(__file__).resolve().parent;repo=here.parents[2];result=args.result.resolve()
if result.exists():raise SystemExit('Use a new result folder')
def git(*args):return subprocess.check_output(['git','-C',str(repo),*args],text=True).strip()
def pin():
    if git('status','--porcelain'):raise RuntimeError('Commit the candidate first')
    return dict(commit=git('rev-parse','HEAD'),files={f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in git('ls-files','--','windows/shim/observer_transport.hpp','windows/host/observer_mapping.hpp','windows/tests/observer_transport').splitlines()})
before=pin();result.mkdir(parents=True);commands=[]
def run(name,cmd,expected=0):
    q=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=90)
    (result/(name+'.log')).write_text(q.stdout+q.stderr)
    commands.append(dict(name=name,exit=q.returncode,command=list(map(str,cmd))))
    if expected is not None and q.returncode!=expected:raise RuntimeError(name+' failed')
    return q
def build(name,source,out):
    run(name+'-configure',['cmake','-S',source,'-B',out,'-G','Visual Studio 17 2022','-A','x64','-T','ClangCL'])
    run(name+'-build',['cmake','--build',out,'--config','Release'])
    return out/'Release/observer_transport.exe'
exe=build('original',here,result/'build')
proof=run('original',[exe]);mutations=[]
plans=[
    ('no-final-sequence-check','if(storage.sequence.load(std::memory_order_seq_cst)==before)','if(true)','changed generation during read'),
    ('accept-in-progress','if(before==0 || (before&1)) continue;','if(before==0) continue;','in-progress bounded read'),
    ('no-thread-owner','if(std::this_thread::get_id()!=thread_ || last_ >','if(last_ >','wrong input thread'),
]
for name,old,new,reason in plans:
    root=result/'mutations'/name
    for rel in ['windows/shim/observer_transport.hpp','windows/host/observer_mapping.hpp','windows/tests/observer_transport/CMakeLists.txt','windows/tests/observer_transport/main.cpp']:
        dst=root/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/rel,dst)
    target=root/'windows/shim/observer_transport.hpp';text=target.read_text();assert text.count(old)==1,name
    target.write_text(text.replace(old,new))
    candidate=build(name,root/'windows/tests/observer_transport',root/'build')
    bad=run(name,[candidate],None)
    if bad.returncode==0 or reason not in bad.stderr:raise RuntimeError(name+' not caught at intended assertion')
    mutations.append(dict(name=name,detected=True,reason=bad.stderr.strip()))
if pin()!=before:raise RuntimeError('Candidate changed during proof')
receipt=dict(source=before,passed=True,proof=proof.stdout.strip(),mutations=mutations,commands=commands,scope='Windows x64 atomic transport only; domain identity, frame encoding and event-ring qualification remain separate')
(result/'receipt.json').write_text(json.dumps(receipt,indent=2))
print(proof.stdout.strip());print('PASS: all three synchronization mutations refused; '+str(result/'receipt.json'))
