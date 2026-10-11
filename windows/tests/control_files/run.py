"""Pinned Win32 snapshot/Core proof using disposable files, not owner data."""
import argparse,hashlib,json,pathlib,shutil,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('result',type=pathlib.Path);args=p.parse_args()
here=pathlib.Path(__file__).resolve().parent;repo=here.parents[2];out=args.result.resolve()
if out.exists():raise SystemExit('Use a fresh result directory')
def git(*a):return subprocess.check_output(['git','-C',str(repo),*a],text=True).strip()
def pin():
    if git('status','--porcelain'):raise RuntimeError('Commit candidate first')
    files=git('ls-files','--','windows/shim','windows/host/control_snapshot.hpp','windows/vendor/controls','windows/tests/shim_fake_provider.hpp','windows/tests/control_files').splitlines()
    return dict(commit=git('rev-parse','HEAD'),files={f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files})
before=pin();out.mkdir(parents=True);commands=[]
def run(name,cmd,expected=0):
    q=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=120)
    (out/(name+'.log')).write_text(q.stdout+q.stderr);commands.append(dict(name=name,exit=q.returncode,command=list(map(str,cmd))))
    if expected is not None and q.returncode!=expected:raise RuntimeError(name+' failed: '+(q.stdout+q.stderr)[-1800:])
    return q
def build(name,src,dest):
    run(name+'-configure',['cmake','-S',src,'-B',dest,'-G','Visual Studio 17 2022','-A','x64','-T','ClangCL'])
    run(name+'-build',['cmake','--build',dest,'--config','Release']);return dest/'Release/control_files.exe'
exe=build('original',here,out/'build');proof=run('original',[exe,out/'files']);mutations=[]
plans=[
    ('bounds','info.nFileSizeHigh||info.nFileSizeLow>bound','false','oversize session refused before read'),
    ('sharing','FILE_SHARE_READ|FILE_SHARE_DELETE','FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE','existing writer refuses snapshot'),
    ('leaf','FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN','FILE_FLAG_SEQUENTIAL_SCAN','leaf reparse point refused'),
]
for name,old,new,why in plans:
    root=out/'mutations'/name
    for f in before['files']:
        dst=root/f;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/f,dst)
    target=root/'windows/host/control_snapshot.hpp';text=target.read_text();assert text.count(old)==1
    target.write_text(text.replace(old,new))
    candidate=build(name,root/'windows/tests/control_files',root/'build');bad=run(name,[candidate,root/'files'],None)
    if bad.returncode==0 or why not in bad.stderr:raise RuntimeError(name+' not caught at its named assertion: '+bad.stderr)
    mutations.append(dict(name=name,caught=True,reason=bad.stderr.strip()))
if pin()!=before:raise RuntimeError('Source changed during proof')
(out/'receipt.json').write_text(json.dumps(dict(source=before,passed=True,proof=proof.stdout.strip(),mutations=mutations,commands=commands),indent=2))
print(proof.stdout.strip());print('PASS three defect controls; '+str(out/'receipt.json'))
