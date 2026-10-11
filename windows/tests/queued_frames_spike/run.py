"""Pin clean source, compose Core snapshots with the reviewed worker, validate and replay offline."""
import argparse, hashlib, json, pathlib, shutil, subprocess

ap=argparse.ArgumentParser()
ap.add_argument('result',type=pathlib.Path)
ap.add_argument('--toolkit',type=pathlib.Path,required=True)
ap.add_argument('--mutations',action='store_true')
args=ap.parse_args();here=pathlib.Path(__file__).resolve().parent;repo=here.parents[2]
result=args.result.resolve();toolkit=args.toolkit.resolve()
if result.exists():raise SystemExit('Use a new result directory.')
def git(root,*args):return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
def pin(root,scopes):
    if git(root,'status','--porcelain','--',*scopes):raise RuntimeError('Uncommitted source: '+str(root))
    return dict(commit=git(root,'rev-parse','HEAD'),sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in git(root,'ls-files','--',*scopes).splitlines()})
tk_scopes=['native/session','dotnet/Dbce.Wheel.Recording','dotnet/Dbce.Wheel.Playback/ArtifactSeal.cs','Directory.*','global.json','NuGet.Config']
before=dict(source=pin(repo,['.']),toolkit=pin(toolkit,tk_scopes))
if before['toolkit']['sha256']['native/session/session_writer.h']!='a9de5d49c55b9b4be464fb173178d20a97972eacff500aee0267e30fb44d19db':
    raise RuntimeError('Unreviewed native writer.')
result.mkdir(parents=True);(result/'source-before.json').write_text(json.dumps(before,indent=2));commands=[]
def run(name,cmd,expected=0):
    p=subprocess.run([str(x) for x in cmd],cwd=repo,capture_output=True,text=True,timeout=180)
    (result/(name+'.log')).write_text(p.stdout+p.stderr)
    commands.append(dict(name=name,command=[str(x) for x in cmd],exit=p.returncode))
    if expected is not None and p.returncode!=expected:raise RuntimeError(name+' failed; see its log')
    return p
def native_build(source,build,prefix):
    run(prefix+'-configure',['cmake','-S',source,'-B',build,'-G','Visual Studio 17 2022','-T','ClangCL','-A','x64',
        '-DTOOLKIT_ROOT='+str(toolkit),'-DSHIM_SOURCE_ROOT='+str(repo)])
    run(prefix+'-build',['cmake','--build',build,'--config','Debug'])
    return build/'Debug/queued_frames_spike.exe'
exe=native_build(here,result/'native','native')
managed=result/'managed'
run('managed-build',['dotnet','build',here/'managed/QueuedConformance.csproj','-c','Release','--artifacts-path',managed,'-p:ToolkitRoot='+str(toolkit)])
dll=managed/'bin/QueuedConformance/release/QueuedConformance.dll'
capture=result/'capture';capture.mkdir()
for mode in ['full','window']:run('record-'+mode,[exe,'record',capture/mode,mode])
run('validate',['dotnet',dll,capture])
for mode in ['full','window']:run('replay-'+mode,[exe,'replay',capture/mode,mode])
mutations=[]
if args.mutations:
    variants={
        'queue-loses-owned-snapshot':('worker.hpp','s_->ring[std::size_t(write%s_->ring.size())]=p;',
            '/* omitted owned copy */','record','eligible actual worker result'),
        'old-enum-refuses-pending':('frame_codec.hpp','int(Call::Pending)','int(Call::NotCalled)','replay','invalid integer: sample.1.poll'),
        'wrong-adapter-record-word':('native.cpp','return writer.sample(double(p.source_us-origin)/1e6,c);',
            'c["adapter.recordSteer"]+=1;return writer.sample(double(p.source_us-origin)/1e6,c);','replay','every original frame and adapter channel preserved'),
    }
    for name,(file,old,new,stage,reason) in variants.items():
        src=result/'mutations'/name;src.mkdir(parents=True)
        for f in ['CMakeLists.txt','native.cpp','frame_codec.hpp']:shutil.copyfile(here/f,src/f)
        if file=='worker.hpp':shutil.copyfile(here.parent/'recorder_lifecycle_spike/worker.hpp',src/file)
        s=(src/file).read_text();count=s.count(old)
        if count!=(2 if file=='frame_codec.hpp' else 1):raise RuntimeError('Mutation anchor changed: '+name)
        (src/file).write_text(s.replace(old,new))
        mx=native_build(src,src/'build',name);mc=src/'capture';mc.mkdir()
        if stage=='record':
            check=run(name+'-record-full',[mx,'record',mc/'full','full'],expected=None)
            if check.returncode==0 or reason not in check.stderr:raise RuntimeError('Mutation not caught by intended check: '+name)
            mutations.append(dict(name=name,detected=True,stage=stage,exit=check.returncode,output=check.stdout+check.stderr))
            continue
        for mode in ['full','window']:run(name+'-record-'+mode,[mx,'record',mc/mode,mode])
        check=run(name+'-validate',['dotnet',dll,mc],expected=None if stage=='validate' else 0)
        if stage=='replay':check=run(name+'-replay',[mx,'replay',mc/'full','full'],expected=None)
        if check.returncode==0 or reason not in check.stderr:raise RuntimeError('Mutation not caught by intended check: '+name)
        mutations.append(dict(name=name,detected=True,stage=stage,exit=check.returncode,output=check.stdout+check.stderr))
after=dict(source=pin(repo,['.']),toolkit=pin(toolkit,tk_scopes))
if before!=after:raise RuntimeError('Source changed during proof.')
evidence={str(p.relative_to(result)):hashlib.sha256(p.read_bytes()).hexdigest() for p in result.rglob('*') if p.is_file() and
    (p.suffix in ['.jsonl','.tsv','.meta','.ini','.log'] or p.name in ['queued_frames_spike.exe','QueuedConformance.dll','Dbce.Wheel.Recording.dll'])}
receipt=dict(sourceCommit=before['source']['commit'],toolkitCommit=before['toolkit']['commit'],sourceVerified=True,
    scope='offline Core/Adapter and fake devices only; no portable replay or game integration',commands=commands,mutations=mutations,sha256=evidence)
(result/'receipt.json').write_text(json.dumps(receipt,indent=2))
for name in ['record-full','record-window','validate','replay-full','replay-window']:print((result/(name+'.log')).read_text().strip())
print('Mutations detected:',len(mutations),'Receipt:',result/'receipt.json')
