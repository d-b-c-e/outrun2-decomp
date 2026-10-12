#include "control_snapshot.hpp"
#include "control_publish.hpp"
#include "shim_fake_provider.hpp"
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
namespace c=dbce::shim::control;
using namespace dbce::shim;
namespace {
int checks=0;
void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
void write(const fs::path& path,const std::string& bytes){
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(f!=INVALID_HANDLE_VALUE,"fixture file created");DWORD n=0;bool ok=WriteFile(f,bytes.data(),DWORD(bytes.size()),&n,nullptr)!=FALSE;CloseHandle(f);
    check(ok&&n==bytes.size(),"fixture file fully written");
}
void files(const fs::path& root){
    c::publish_snapshot((root/L"old").wstring(),"old bytes");
    {c::SnapshotFile original((root/L"old").wstring(),512);
     c::publish_snapshot((root/L"old").wstring(),"new bytes");
     auto old=original.take();check(old.state==c::ReadState::Bytes&&old.bytes=="old bytes","open handle reads old complete object after replacement");
     check(original.take().state==c::ReadState::Error,"snapshot consumed once");}
    check(c::read_snapshot((root/L"old").wstring(),512).bytes=="new bytes","next open sees replacement");
    write(root/L"limit",std::string(512,'x'));write(root/L"oversize",std::string(513,'x'));write(root/L"batch",std::string(4096,'b'));write(root/L"empty","");
    check(c::read_snapshot((root/L"limit").wstring(),512).bytes.size()==512,"exact session bound accepted");
    check(c::read_snapshot((root/L"oversize").wstring(),512).state==c::ReadState::Error,"oversize session refused before read");
    check(c::read_snapshot((root/L"batch").wstring(),4096).bytes.size()==4096,"exact batch bound accepted");
    check(c::read_snapshot((root/L"batch").wstring(),4097).state==c::ReadState::Error,"unbounded caller refused");
    check(c::read_snapshot((root/L"empty").wstring(),512).state==c::ReadState::Bytes,"empty differs from missing for parser refusal");
    check(c::read_snapshot((root/L"missing").wstring(),512).state==c::ReadState::Missing,"not found reported separately");
    check(c::read_snapshot(root.wstring(),512).state==c::ReadState::Error,"directory refused");
    check(c::read_snapshot((root/L"old:stream").wstring(),512).state==c::ReadState::Error,"alternate stream refused");
    HANDLE writer=CreateFileW((root/L"old").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    check(writer!=INVALID_HANDLE_VALUE,"competing writer fixture open");
    auto denied=c::read_snapshot((root/L"old").wstring(),512);CloseHandle(writer);
    check(denied.state==c::ReadState::Error&&denied.bytes.empty(),"existing writer refuses snapshot");
    {c::SnapshotFile held((root/L"old").wstring(),512);
     writer=CreateFileW((root/L"old").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
     check(writer==INVALID_HANDLE_VALUE,"snapshot refuses new in-place writer");
     check(held.take().bytes=="new bytes","writer refusal leaves bytes intact");}
    // No skip: this qualification environment must support a leaf symlink.
    check(CreateSymbolicLinkW((root/L"link").c_str(),(root/L"old").c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=FALSE,"leaf link fixture created");
    check(c::read_snapshot((root/L"link").wstring(),512).state==c::ReadState::Error,"leaf reparse point refused");
}
BOOL WINAPI unsupported(HANDLE,FILE_INFO_BY_HANDLE_CLASS,LPVOID,DWORD){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
void publication(const fs::path& root){
    const auto path=(root/L"concurrent").wstring();
    const std::string first="generation A\n"+std::string(4000,'a'),second="generation B\n"+std::string(37,'b');
    c::publish_snapshot(path,first);
    {c::SnapshotFile held(path,MaxBatchBytes);
     for(int i=0;i<400;++i)c::publish_snapshot(path,i%2?first:second);
     check(held.take().bytes==first,"held reader retains original through 400 replacements");}
    DWORD handles_before=0;
    check(GetProcessHandleCount(GetCurrentProcess(),&handles_before)!=FALSE,"handle baseline captured after warmup");
    std::atomic<bool> start{},done{};std::atomic<int> ready{};
    std::array<std::string,2> errors;std::array<unsigned,2> reads{};
    std::array<std::thread,2> readers;
    for(std::size_t n=0;n<readers.size();++n)readers[n]=std::thread([&,n]{
        ++ready;while(!start.load())std::this_thread::yield();
        do{
            auto result=c::read_snapshot(path,MaxBatchBytes);++reads[n];
            if(result.state!=c::ReadState::Bytes){errors[n]="concurrent SnapshotFile error: "+result.reason;break;}
            if(result.bytes!=first&&result.bytes!=second){errors[n]="concurrent partial or foreign bytes";break;}
        }while(!done.load()||reads[n]<1000);
    });
    while(ready.load()!=2)std::this_thread::yield();start=true;
    std::string writer_error;
    try{for(int i=0;i<2000;++i)c::publish_snapshot(path,i%2?first:second);}
    catch(const std::exception& failure){writer_error=failure.what();}
    done=true;for(auto& reader:readers)reader.join();
    if(!writer_error.empty())throw std::runtime_error(writer_error);
    for(const auto& error:errors)if(!error.empty())throw std::runtime_error(error);
    check(reads[0]>=1000&&reads[1]>=1000,"both actual SnapshotFile readers exercised concurrently");
    check(c::read_snapshot(path,MaxBatchBytes).bytes==first,"latest generation survives concurrent readers");
    bool refused=false;
    try{c::publish_detail::publish(path,second,&unsupported);}
    catch(const std::exception& error){refused=std::string(error.what()).find("FileRenameInfoEx failed: 50")!=std::string::npos;}
    check(refused,"unsupported rename explicitly refused without fallback");
    check(c::read_snapshot(path,MaxBatchBytes).bytes==first,"unsupported rename preserves target bytes");
    const auto directory=root/L"cannot-replace";fs::create_directory(directory);
    refused=false;try{c::publish_snapshot(directory.wstring(),second);}catch(const std::exception&){refused=true;}
    check(refused&&fs::is_directory(directory),"directory target refused and preserved");
    refused=false;try{c::publish_snapshot(path,std::string(MaxBatchBytes+1,'x'));}catch(const std::invalid_argument&){refused=true;}
    check(refused&&c::read_snapshot(path,MaxBatchBytes).bytes==first,"oversize publication cannot replace target");
    for(const auto& entry:fs::directory_iterator(root))
        check(entry.path().filename().wstring().find(L".publish-")==std::wstring::npos,"no owned publication temp remains after success or refusal");
    {c::publish_detail::Handle exclusive;
     exclusive.value=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
     check(exclusive.value!=INVALID_HANDLE_VALUE,"latest object has no retained publisher or reader handle");}
    check(DeleteFileW(path.c_str())!=FALSE,"publication and reader handles released for cleanup");
    DWORD handles_after=0;
    check(GetProcessHandleCount(GetCurrentProcess(),&handles_after)!=FALSE&&handles_after<=handles_before,
        "concurrent publication and refusal do not leak old-object handles");
    std::cout<<"PASS 2000 concurrent publications / "<<reads[0]+reads[1]<<" actual SnapshotFile reads (zero errors), no handle growth\n";
}
struct FileIo final:c::Io {
    fs::path root;int sessions{},batches{},logs{};std::vector<c::Journal> journal;
    explicit FileIo(fs::path r):root(std::move(r)){}
    c::Read read_session()override{++sessions;return c::read_snapshot((root/L"inject.on").wstring(),512);}
    c::Read read_batch()override{++batches;return c::read_snapshot((root/L"inject.txt").wstring(),MaxBatchBytes);}
    std::uint64_t unix_seconds()override{return 1000;}
    bool append(const c::Journal& j)override{++logs;journal.push_back(j);return true;}
    bool finish()override{return true;}
};
void integrated(const fs::path& root){
    const std::string dev="{11111111-1111-1111-1111-111111111111}",prod="{22222222-2222-2222-2222-222222222222}";
    fake::Provider p;fake::Device d;d.listed={dev,prod,"fake"};d.caps=fake::wheel_caps();p.add(d);
    CoreConfig cfg;cfg.latched=true;Core core(p,cfg);
    core.set_profile(read_profile("[Controls]\nSchema=1\nconfirm=button 0 dev="+dev+" prod="+prod+"\n"));
    FileIo io(root);c::Pump pump(core,io,{true,true,true,"BoundedFileTest"});
    write(root/L"inject.on","nonce=BoundedFileTest\nexpires=2000\n");
    const auto command="nonce=BoundedFileTest\nbatch=1\ninject raw button 0 dev="+dev+" value=1 ms=50\n";
    write(root/L"inject.txt",command);pump.pass();check(io.batches==0,"disk command waits for Core admission");
    core.service_devices();check(core.tick().run==Run::Waiting,"disk session admitted");pump.pass();
    const auto& frame=core.tick();check(frame.run==Run::Running&&frame.action[std::size_t(action_index("confirm"))].value==1,"disk raw batch reaches actual resolver");
    pump.pass();p.advance(60000);check(core.tick().action[std::size_t(action_index("confirm"))].value==0,"disk repeat cannot extend TTL");
    c::publish_snapshot((root/L"inject.txt").wstring(),"nonce=BoundedFileTest\nbatch=2\nend\n");
    pump.pass();check(core.tick().run==Run::Completed,"disk end completes actual Core");pump.pass();
    check(io.sessions==1&&io.batches==3,"one session snapshot and bounded batch polling");
    bool recorded=false;for(const auto& j:io.journal)for(const auto& e:j.events)if(e.kind==EventKind::BatchAccepted&&e.detail.find(hex(sha256(command)))!=std::string::npos)recorded=true;
    check(recorded,"original disk bytes joined by full batch hash");
}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=2)throw std::runtime_error("fresh result folder required");fs::path root(argv[1]);check(!fs::exists(root),"fixture folder must be new");fs::create_directories(root);files(root);publication(root);integrated(root);std::cout<<"PASS "<<checks<<" Win32 snapshot/Core checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
