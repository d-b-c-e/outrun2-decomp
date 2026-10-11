"""Production control-worker lifetime over fake COM and I/O, supervised children."""
import argparse,ctypes,hashlib,json,pathlib,shutil,subprocess,sys,time
p=argparse.ArgumentParser(description=__doc__);p.add_argument('result',type=pathlib.Path);args=p.parse_args()
here=pathlib.Path(__file__).resolve().parent;repo=here.parents[2];out=args.result.resolve()
if out.exists():raise SystemExit('Use a fresh result directory')
def git(*a):return subprocess.check_output(['git','-C',str(repo),*a],text=True).strip()
def pin():
    if git('status','--porcelain'):raise RuntimeError('Commit candidate first')
    files=git('ls-files','--','windows/shim','windows/host','windows/vendor/controls','windows/tests/shim_fake_com.hpp','windows/tests/control_host','windows/tests/teardown_audit*','windows/tests/teardown_objects*','windows/tests/teardown_units').splitlines()
    return dict(commit=git('rev-parse','HEAD'),files={f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files})
before=pin();out.mkdir(parents=True);commands=[]
def run(name,cmd,expected=0):
    q=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=240)
    (out/(name+'.log')).write_text(q.stdout+q.stderr);commands.append(dict(name=name,exit=q.returncode,command=list(map(str,cmd))))
    if expected is not None and q.returncode!=expected:raise RuntimeError(name+' failed: '+(q.stdout+q.stderr)[-1800:])
    return q
def build(name,src,dest):
    run(name+'-configure',['cmake','-S',src,'-B',dest,'-G','Visual Studio 17 2022','-A','x64','-T','ClangCL'])
    run(name+'-build',['cmake','--build',dest,'--config','Release']);return dest/'Release/control_host.exe'
def child(exe,mode,dest):
    t=time.monotonic();q=subprocess.Popen([str(exe),mode,str(dest)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    # FILETIME is an unsigned 64-bit Windows count, distinct from a .NET date.
    times=[ctypes.c_ulonglong() for _ in range(4)]
    get=ctypes.WinDLL('kernel32',use_last_error=True).GetProcessTimes
    get.argtypes=[ctypes.c_void_p]+[ctypes.POINTER(ctypes.c_ulonglong)]*4
    if not get(int(q._handle),*(ctypes.byref(v) for v in times)):q.kill();q.wait();raise RuntimeError('Child identity unreadable')
    forced=False
    try:stdout,stderr=q.communicate(timeout=4)
    except subprocess.TimeoutExpired:
        q.kill();stdout,stderr=q.communicate(timeout=5);forced=True
    host=dest/'host-shutdown.json';report=json.loads(host.read_text()) if host.exists() else None
    result=dict(mode=mode,pid=q.pid,creationTime=times[0].value,exit=q.returncode,forced=forced,elapsed=time.monotonic()-t,stdout=stdout,stderr=stderr,host=report)
    (dest.parent/(dest.name+'-supervisor.json')).write_text(json.dumps(result,indent=2))
    return result
exe=build('original',here,out/'build');cases=[]
for mode in ['resolved','write-failed','finish-failed','read-blocked','finish-blocked','thread-tail','late-read','missing-final-tick']:
    r=child(exe,mode,out/mode);h=r['host'];good=mode=='resolved'
    if r['stderr'] or r['forced']==good or (good and r['exit']!=0):raise RuntimeError(mode+' child failure '+r['stderr'])
    if not h or h['pid']!=r['pid'] or h['creationTime']!=r['creationTime'] or h['nonce']!='ControlWorkerFixture':raise RuntimeError('Identity mismatch: '+mode)
    if h['state']!=('resolved' if good else 'unresolved') or not h['inputReleased'] or not h['controlPresent'] or h['elapsedUs']>1_850_000:raise RuntimeError('Stop report mismatch: '+mode)
    joined=mode in ['resolved','write-failed','finish-failed','missing-final-tick'];complete=mode in ['resolved','thread-tail','missing-final-tick']
    if h['controlJoined']!=joined or h['controlComplete']!=complete or h['controlFinalTick']!=(mode!='missing-final-tick'):raise RuntimeError('Outcome/join mismatch: '+mode)
    cases.append(r)
for script in ['teardown_audit.py','teardown_objects.py']:
    run(script,[sys.executable,'-B',repo/'windows/tests'/script,repo]);run(script+'-mutations',[sys.executable,'-B',repo/'windows/tests'/script,repo,'--mutations'])
mutations=[]
plans=[
    ('thread-tail','windows/host/provider_host.hpp','r.control_joined=returned&&join_by(control_thread_,deadline);','r.control_joined=returned;','thread-tail','fault cannot qualify host'),
    ('journal-failure','windows/host/provider_host.hpp','if(!r.control_complete)r.unresolved.push_back("control journal incomplete");','if(!r.control_complete){}','write-failed','fault cannot qualify host'),
    ('final-drain','windows/shim/control_pump.hpp','const bool written=io_.append(drain());','const bool written=true;','resolved','final journal and sink close observed'),
    ('final-tick','windows/host/provider_host.hpp','if(!r.control_final_tick)r.unresolved.push_back("control closure has no final input tick");','if(!r.control_final_tick){}','missing-final-tick','fault cannot qualify host'),
]
for name,rel,old,new,mode,why in plans:
    root=out/'mutations'/name
    for f in before['files']:
        dst=root/f;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/f,dst)
    target=root/rel;text=target.read_text();assert text.count(old)==1;target.write_text(text.replace(old,new))
    candidate=build(name,root/'windows/tests/control_host',root/'build');bad=child(candidate,mode,root/'run')
    if why not in bad['stderr']:raise RuntimeError(name+' not caught at its named assertion: '+bad['stderr'])
    mutations.append(dict(name=name,caught=True,reason=bad['stderr'].strip()))
if pin()!=before:raise RuntimeError('Source changed during proof')
(out/'receipt.json').write_text(json.dumps(dict(source=before,passed=True,executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),cases=cases,mutations=mutations,commands=commands),indent=2))
print('PASS eight control-host child cases, teardown audits and four defect controls; '+str(out/'receipt.json'))
