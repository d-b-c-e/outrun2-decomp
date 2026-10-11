"""Pinned offline observer domain checks and reader defect controls."""
import argparse,hashlib,json,pathlib,shutil,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('result',type=pathlib.Path);args=p.parse_args()
here=pathlib.Path(__file__).resolve().parent;repo=here.parents[2];out=args.result.resolve()
if out.exists():raise SystemExit('Use a fresh result directory')
def git(*a):return subprocess.check_output(['git','-C',str(repo),*a],text=True).strip()
def pin():
    if git('status','--porcelain'):raise RuntimeError('Commit candidate first')
    files=git('ls-files','--','windows/shim','windows/vendor/controls','windows/tests/shim_fake_provider.hpp','windows/tests/observer_domain').splitlines()
    return dict(commit=git('rev-parse','HEAD'),files={f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files})
before=pin();out.mkdir(parents=True);commands=[]
def run(name,cmd,expected=0):
    q=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=90)
    (out/(name+'.log')).write_text(q.stdout+q.stderr);commands.append(dict(name=name,exit=q.returncode,command=list(map(str,cmd))))
    if expected is not None and q.returncode!=expected:raise RuntimeError(name+' failed: '+(q.stdout+q.stderr)[-1500:])
    return q
def build(name,src,dest):
    run(name+'-configure',['cmake','-S',src,'-B',dest,'-G','Visual Studio 17 2022','-A','x64','-T','ClangCL'])
    run(name+'-build',['cmake','--build',dest,'--config','Release']);return dest/'Release/observer_domain.exe'
exe=build('original',here,out/'build');proof=run('original',[exe]);mutations=[]
plans=[
    ('identity','if(!(next.identity==expected_))','if(false)','wrong run/process/build/profile identity'),
    ('freshness','now_us-next.boundary_us>freshness_us','false','stale not displayed as new'),
    ('ring-gap','return {Acceptance::Accepted,gap};','return {Acceptance::Accepted,0};','first reader detects six overwritten events'),
    ('signal-offset','std::uint64_t(-std::int64_t(s.offset))!=d.frame-s.frame','false','mismatched signal source frame'),
]
for name,old,new,why in plans:
    root=out/'mutations'/name
    for rel in before['files']:
        dst=root/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/rel,dst)
    target=root/'windows/shim/observer_domain.hpp';text=target.read_text();assert text.count(old)==1
    # Keep the measured gap local used to avoid a warning-only failure.
    if name=='ring-gap':new='return {Acceptance::Accepted,gap-gap};'
    target.write_text(text.replace(old,new))
    candidate=build(name,root/'windows/tests/observer_domain',root/'build');bad=run(name,[candidate],None)
    if bad.returncode==0 or why not in bad.stderr:raise RuntimeError(name+' not caught at its named assertion')
    mutations.append(dict(name=name,caught=True,reason=bad.stderr.strip()))
if pin()!=before:raise RuntimeError('Source changed during proof')
(out/'receipt.json').write_text(json.dumps(dict(source=before,passed=True,proof=proof.stdout.strip(),mutations=mutations,commands=commands),indent=2))
print(proof.stdout.strip());print('PASS four reader defect controls; '+str(out/'receipt.json'))
