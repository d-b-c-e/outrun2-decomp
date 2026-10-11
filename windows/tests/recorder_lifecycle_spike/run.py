"""Build an exact committed offline lifecycle fixture; all outputs use a new result directory."""
import argparse, hashlib, json, pathlib, shutil, subprocess

parser=argparse.ArgumentParser()
parser.add_argument('result', type=pathlib.Path)
parser.add_argument('--toolkit', required=True, type=pathlib.Path)
parser.add_argument('--mutations', action='store_true')
args=parser.parse_args()
here=pathlib.Path(__file__).resolve().parent
repo=here.parents[2]
result=args.result.resolve(); toolkit=args.toolkit.resolve()
if result.exists(): raise SystemExit('Use a new result directory.')
def git(root,*args): return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
def pin(root,scopes):
    if git(root,'status','--porcelain','--',*scopes): raise RuntimeError('Uncommitted input: '+str(root))
    files=git(root,'ls-files','--',*scopes).splitlines()
    return dict(commit=git(root,'rev-parse','HEAD'),sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files})
toolkit_scopes=['native/session','dotnet/Dbce.Wheel.Recording','Directory.*','global.json','NuGet.Config']
before=dict(source=pin(repo,['.']),toolkit=pin(toolkit,toolkit_scopes))
if before['toolkit']['sha256']['native/session/session_writer.h']!='a9de5d49c55b9b4be464fb173178d20a97972eacff500aee0267e30fb44d19db':
    raise RuntimeError('Native writer differs from reviewed pin.')
result.mkdir(parents=True)
(result/'source-before.json').write_text(json.dumps(before,indent=2))
commands=[]
def run(name,command,expect=0):
    p=subprocess.run([str(x) for x in command],cwd=repo,capture_output=True,text=True,timeout=180)
    (result/(name+'.log')).write_text(p.stdout+p.stderr)
    commands.append(dict(name=name,command=[str(x) for x in command],exit=p.returncode))
    if expect is not None and p.returncode!=expect: raise RuntimeError(name+' failed; see its log')
    return p
build=result/'build'; managed=result/'managed'; capture=result/'capture'
run('configure',['cmake','-S',here,'-B',build,'-G','Visual Studio 17 2022','-T','ClangCL','-A','x64','-DTOOLKIT_ROOT='+str(toolkit)])
run('build',['cmake','--build',build,'--config','Debug'])
run('lifecycle',[build/'Debug/recorder_lifecycle_test.exe'])
run('native',[build/'Debug/recorder_lifecycle_native.exe',capture])
run('managed-build',['dotnet','build',here/'managed/LifecycleConformance.csproj','--configuration','Release','--artifacts-path',managed,'-p:ToolkitRoot='+str(toolkit)])
run('reader',['dotnet',managed/'bin/LifecycleConformance/release/LifecycleConformance.dll',capture])
mutations=[]
if args.mutations:
    original=(here/'worker.hpp').read_text()
    variants={
      'late-completion-overwrites-timeout': [('s->outcome.compare_exchange_strong(draining,Outcome::Complete);','s->outcome.store(Outcome::Complete);(void)draining;')],
      'overflow-not-sticky': [('s_->fail(Outcome::QueueFull);','/* dropped without latching failure */')],
      'trust-accepted-tail': [('s_->declared_last=last_source_sequence;','s_->declared_last=next_-1;(void)last_source_sequence;'),('else if(last_source_sequence!=next_-1)s_->fail(Outcome::TrailingLoss);','/* ignore declared producer tail */')],
      'ignore-producer-thread': [('correct=s.producer_thread==std::this_thread::get_id();','correct=true;')],
      'omit-final-publication-read': [('if(r!=s->write.load(std::memory_order_acquire))continue;','/* stale empty observation */')],
      'omit-owned-copy': [('s_->ring[std::size_t(write%s_->ring.size())]=p;','/* snapshot copy omitted */')],
    }
    for name,edits in variants.items():
        source=result/'mutations'/name; source.mkdir(parents=True)
        changed=original
        for old,new in edits:
            if changed.count(old)!=1: raise RuntimeError('Mutation anchor changed: '+name)
            changed=changed.replace(old,new)
        (source/'worker.hpp').write_text(changed)
        for file in ['test.cpp','CMakeLists.txt']: shutil.copyfile(here/file,source/file)
        b=source/'build'
        run(name+'-configure',['cmake','-S',source,'-B',b,'-G','Visual Studio 17 2022','-T','ClangCL','-A','x64'])
        run(name+'-build',['cmake','--build',b,'--config','Debug'])
        p=run(name,[b/'Debug/recorder_lifecycle_test.exe'],expect=None)
        if p.returncode==0: raise RuntimeError('Mutation escaped: '+name)
        mutations.append(dict(name=name,detected=True,exit=p.returncode,output=p.stdout+p.stderr))
after=dict(source=pin(repo,['.']),toolkit=pin(toolkit,toolkit_scopes))
if before!=after: raise RuntimeError('Inputs changed during the proof.')
artifacts={str(p.relative_to(result)):hashlib.sha256(p.read_bytes()).hexdigest() for p in result.rglob('*') if p.is_file() and (p.suffix in ['.jsonl','.tsv','.log'] or p.name in ['recorder_lifecycle_test.exe','recorder_lifecycle_native.exe','LifecycleConformance.dll','Dbce.Wheel.Recording.dll'])}
receipt=dict(sourceCommit=before['source']['commit'],toolkitCommit=before['toolkit']['commit'],sourceVerified=True,commands=commands,mutations=mutations,sha256=artifacts)
(result/'receipt.json').write_text(json.dumps(receipt,indent=2))
print((result/'lifecycle.log').read_text().strip())
print((result/'reader.log').read_text().strip())
print('Negative controls detected:',len(mutations),'Receipt:',result/'receipt.json')
