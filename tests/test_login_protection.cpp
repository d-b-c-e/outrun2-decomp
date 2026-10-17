#include "platform/frontend_profiles.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
using namespace outrun::platform;
namespace fs=std::filesystem;
namespace {
unsigned checks{};
void req(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
using Bytes=std::vector<std::uint8_t>;
struct Fixture {
    fs::path root=fs::temp_directory_path()/("or2-login-synthetic-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){fs::create_directory(root);}
    ~Fixture(){frontend_profiles_set_login_protection({});fs::remove_all(root);}
    std::string dir()const{return root.string();}
};
Bytes bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void raw(const fs::path& path,const Bytes& data){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));req(bool(out),"private synthetic fixture written");}
FrontendProfiles profile(){FrontendProfiles p;p.selected=2;p.active_loaded=true;p.active.fill(0x71);
    for(std::size_t n=0;n<p.common.size();++n)p.common[n]=static_cast<std::uint8_t>((n%251)+1);
    return p;
}
// Deliberately fake reversible transform, not encryption. Never links/runs DPAPI.
FrontendLoginProtection fake(){FrontendLoginProtection p;p.enabled=true;
    p.protect=[](const auto* data,auto n,auto& out){out={0xa5,0x5a};for(std::size_t k=0;k<n;++k)out.push_back(data[k]^0xd3);return true;};
    p.unprotect=[](const auto* data,auto n,auto& out){if(n<2||data[0]!=0xa5||data[1]!=0x5a)return false;
        for(std::size_t k=2;k<n;++k)out.push_back(data[k]^0xd3);return true;};
    return p;
}
bool login_zero(const Bytes& common){return common.size()==PcCommonSaveBytes+4&&std::all_of(common.begin()+4+PcLoginOffset,common.begin()+4+PcLoginOffset+PcLoginBytes,[](auto b){return b==0;});}
void outside_equal(const Bytes& disk,const FrontendProfiles& p){req(disk.size()==PcCommonSaveBytes+4,"PC common envelope unchanged");
    for(std::size_t n=0;n<p.common.size();++n)if(n<PcLoginOffset||n>=PcLoginOffset+PcLoginBytes)
        if(disk[n+4]!=p.common[n])throw std::runtime_error("bytes outside login block changed");
    ++checks;
}
}
int main(){try{
    {
        Fixture f;auto p=profile();const auto original=p.common;
        auto result=frontend_profiles_save_common_4164d0(p,f.dir());
        req(bool(result)&&result.common_written,"default common save succeeds");
        const auto plain=bytes(f.root/"common.dat");req(!login_zero(plain),"default plaintext behavior retained");
        req(!fs::exists(f.root/"OnlineLoginData.dat"),"default creates no sidecar");
        raw(f.root/"OnlineLoginData.dat",{0xde,0xad});
        FrontendProfiles loaded;frontend_profiles_load(loaded,f.dir());
        req(loaded.common==p.common&&!loaded.login_error,"default ignores even invalid sidecar");
        frontend_profiles_set_login_protection(fake());frontend_profiles_load(loaded,f.dir());
        req(loaded.login_error&&loaded.invalid_files==1,"enabled invalid sidecar reports error");
        req(std::all_of(loaded.common.begin()+PcLoginOffset,loaded.common.begin()+PcLoginOffset+PcLoginBytes,[](auto b){return !b;}),"bad decrypt clears live login");
        req(bytes(f.root/"common.dat")==plain,"load never migrates or scrubs files");
        req(!frontend_profiles_save_common_4164d0(loaded,f.dir()),"failed-load latch refuses save");
        req(bytes(f.root/"OnlineLoginData.dat")==Bytes({0xde,0xad}),"bad sidecar retained for recovery");
        req(bytes(f.root/"common.dat")==plain,"failed-load save retains original common");
        fs::remove(f.root/"OnlineLoginData.dat");frontend_profiles_reload_416380(loaded,f.dir());
        req(!loaded.login_error&&std::equal(original.begin()+PcLoginOffset,original.begin()+PcLoginOffset+PcLoginBytes,loaded.common.begin()+PcLoginOffset),"missing sidecar retains legacy live login");
        req(!fs::exists(f.root/"OnlineLoginData.dat"),"missing-sidecar load stays read-only");
        result=frontend_profiles_save_common_4164d0(loaded,f.dir());req(bool(result),"first explicit protected save succeeds");
        req(login_zero(bytes(f.root/"common.dat")),"first protected save scrubs all 192 login bytes");
        outside_equal(bytes(f.root/"common.dat"),loaded);
        req(bytes(f.root/"OnlineLoginData.dat").size()==PcLoginBytes+2,"cipher sidecar is raw, no PC envelope");
        FrontendProfiles restored;frontend_profiles_load(restored,f.dir());
        req(restored.common==loaded.common&&!restored.login_error,"cold protected load restores exact live common");
        req(!fs::exists(f.root/"common.dat.bak")&&!fs::exists(f.root/"OnlineLoginData.dat.bak"),"successful replace cleans recovery backups");
    }
    for(const bool license:{false,true}){
        Fixture f;auto p=profile();frontend_profiles_set_login_protection(fake());
        const auto initial=p.common;
        const auto result=license?frontend_profiles_save_416420(p,f.dir(),true):frontend_profiles_save_common_4164d0(p,f.dir());
        req(bool(result)&&result.common_written,"both original common write entry points protected");
        req(result.license_written==license,"license write semantics preserved");
        req(login_zero(bytes(f.root/"common.dat")),"both paths write zero login block");outside_equal(bytes(f.root/"common.dat"),p);
        req(std::equal(initial.begin()+PcLoginOffset,initial.begin()+PcLoginOffset+PcLoginBytes,p.common.begin()+PcLoginOffset),"live login never scrubbed during save");
        FrontendProfiles loaded;frontend_profiles_load(loaded,f.dir());req(loaded.common==p.common,"both paths round-trip live login");
    }
    {
        Fixture f;auto p=profile();frontend_profiles_set_login_protection(fake());
        req(bool(frontend_profiles_save_416420(p,f.dir(),false)),"disabled writer succeeds without effects");
        req(fs::is_empty(f.root),"disabled writes invoke no sidecar or common writes");
        frontend_profiles_set_login_protection({});req(bool(frontend_profiles_save_common_4164d0(p,f.dir())),"seed legacy common");
        const auto old=bytes(f.root/"common.dat");
        for(unsigned mode=0;mode<5;++mode){auto policy=fake();
            policy.protect=[mode](const auto*,auto,auto& out)->bool{
                if(mode==0)return false;if(mode==1)throw std::runtime_error("fake crypto failure");
                if(mode==2){out.clear();return true;}out.assign(PcLoginCipherLimit+1,0);return true;
            };
            if(mode==4)policy.protect={};frontend_profiles_set_login_protection(policy);
            const auto result=frontend_profiles_save_common_4164d0(p,f.dir());
            req(!result&&!result.common_written&&result.common_error,"crypto failure refuses common write");
            req(bytes(f.root/"common.dat")==old&&!fs::exists(f.root/"OnlineLoginData.dat"),"no plaintext fallback or partial sidecar");
        }
        frontend_profiles_set_login_protection(fake());raw(f.root/"OnlineLoginData.dat.bak",{0x42});
        req(!frontend_profiles_save_common_4164d0(p,f.dir()),"sidecar recovery collision refuses common write");
        req(bytes(f.root/"common.dat")==old&&bytes(f.root/"OnlineLoginData.dat.bak")==Bytes({0x42}),"sidecar backup and original common preserved");
        fs::remove(f.root/"OnlineLoginData.dat.bak");raw(f.root/"common.dat.bak",{0x43});
        req(!frontend_profiles_save_common_4164d0(p,f.dir()),"common recovery collision reported");
        req(bytes(f.root/"common.dat")==old&&bytes(f.root/"common.dat.bak")==Bytes({0x43}),"common failure preserves prior common and backup");
    }
    for(unsigned mode=0;mode<6;++mode){
        Fixture f;auto p=profile();auto policy=fake();frontend_profiles_set_login_protection(policy);
        req(bool(frontend_profiles_save_common_4164d0(p,f.dir())),"seed fake protected save");
        if(mode==0)raw(f.root/"OnlineLoginData.dat",{});
        if(mode==1)raw(f.root/"OnlineLoginData.dat",Bytes(PcLoginCipherLimit+1,1));
        if(mode==2)policy.unprotect=[](const auto*,auto,auto& out){out.assign(PcLoginBytes-1,7);return true;};
        if(mode==3)policy.unprotect=[](const auto*,auto,auto& out){out.assign(PcLoginBytes+1,7);return true;};
        if(mode==4)policy.unprotect=[](const auto*,auto,auto& out)->bool{out.assign(PcLoginBytes,7);throw std::runtime_error("fake decrypt failure");};
        if(mode==5)policy.unprotect={};
        frontend_profiles_set_login_protection(policy);const auto saved=bytes(f.root/"OnlineLoginData.dat");
        frontend_profiles_load(p,f.dir());req(p.login_error!=0,"empty, oversized, short, long, throwing or missing decrypt rejected");
        req(!frontend_profiles_save_common_4164d0(p,f.dir()),"invalid protected payload cannot be overwritten");
        req(bytes(f.root/"OnlineLoginData.dat")==saved,"invalid sidecar unchanged");
    }
    std::printf("login_protection: %u checks; synthetic bytes/fake crypto only, no DPAPI, network, game or owner files\n",checks);return 0;
}catch(const std::exception& e){frontend_profiles_set_login_protection({});std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
