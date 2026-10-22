// Qualifies the real DPAPI adapter (login_protection.cpp) through the shared profile consumers.
// Disposable synthetic 192-byte login data in a private temp directory only; no owner saves/accounts.
#include "login_protection.hpp"
#include "platform/frontend_profiles.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
using namespace outrun::platform;
using outrun::desktop::Ini;
using outrun::desktop::login_protection_configure;
namespace fs=std::filesystem;
namespace {
unsigned checks{};
void req(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
using Bytes=std::vector<std::uint8_t>;
struct Fixture {
    fs::path root=fs::temp_directory_path()/("or2-dpapi-synthetic-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){fs::create_directory(root);}
    ~Fixture(){frontend_profiles_set_login_protection({});std::error_code ec;fs::remove_all(root,ec);}
    std::string dir()const{return root.string();}
};
Bytes bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void raw(const fs::path& path,const Bytes& data){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));req(bool(out),"private synthetic fixture written");}
Ini ini(const char* value){Ini i;i.load_text(std::string("[Misc]\nProtectLoginData=")+value+"\n","synthetic.ini");return i;}
FrontendProfiles profile(){FrontendProfiles p;p.selected=2;p.active_loaded=true;p.active.fill(0x71);
    for(std::size_t n=0;n<p.common.size();++n)p.common[n]=static_cast<std::uint8_t>((n%251)+1);
    for(std::size_t n=0;n<PcLoginBytes;++n)p.common[PcLoginOffset+n]=static_cast<std::uint8_t>(0x30+(n*7)%0x4b); // synthetic, no zero bytes
    return p;
}
bool login_zero(const Bytes& common){return common.size()==PcCommonSaveBytes+4&&std::all_of(common.begin()+4+PcLoginOffset,common.begin()+4+PcLoginOffset+PcLoginBytes,[](auto b){return b==0;});}
bool contains(const Bytes& hay,const std::uint8_t* needle,std::size_t n){return std::search(hay.begin(),hay.end(),needle,needle+n)!=hay.end();}
void outside_equal(const Bytes& disk,const FrontendProfiles& p){req(disk.size()==PcCommonSaveBytes+4,"PC common envelope unchanged");
    for(std::size_t n=0;n<p.common.size();++n)if(n<PcLoginOffset||n>=PcLoginOffset+PcLoginBytes)
        if(disk[n+4]!=p.common[n])throw std::runtime_error("bytes outside login block changed");
    ++checks;
}
}
int main(){try{
    // Default Off and excluded process (ordinary=false) leave plaintext behavior and create no sidecar.
    for(const auto cfg:{0,1,2}){
        Fixture f;auto p=profile();
        if(cfg==0)login_protection_configure(Ini{},true);
        if(cfg==1)login_protection_configure(ini("false"),true);
        if(cfg==2)login_protection_configure(ini("true"),false);
        req(bool(frontend_profiles_save_common_4164d0(p,f.dir())),"off save succeeds");
        req(!login_zero(bytes(f.root/"common.dat"))&&!fs::exists(f.root/"OnlineLoginData.dat"),"off/excluded keeps plaintext, no sidecar");
        FrontendProfiles loaded;frontend_profiles_load(loaded,f.dir());req(loaded.common==p.common,"off round trip");
    }
    for(const bool license:{false,true}){
        Fixture f;auto p=profile();const auto initial=p.common;
        login_protection_configure(ini("true"),true);
        const auto result=license?frontend_profiles_save_416420(p,f.dir(),true):frontend_profiles_save_common_4164d0(p,f.dir());
        req(bool(result)&&result.common_written,"DPAPI protected save succeeds");
        const auto disk=bytes(f.root/"common.dat"),side=bytes(f.root/"OnlineLoginData.dat");
        req(login_zero(disk),"common.dat login region scrubbed");outside_equal(disk,p);
        req(std::equal(initial.begin()+PcLoginOffset,initial.begin()+PcLoginOffset+PcLoginBytes,p.common.begin()+PcLoginOffset),"live login bytes retained");
        req(side.size()>PcLoginBytes&&!contains(side,&initial[PcLoginOffset],16)&&!contains(side,&initial[PcLoginOffset+PcLoginBytes-16],16),"sidecar is non-plaintext");
        FrontendProfiles loaded;frontend_profiles_load(loaded,f.dir());
        req(!loaded.login_error&&loaded.common==p.common&&std::equal(initial.begin()+PcLoginOffset,initial.begin()+PcLoginOffset+PcLoginBytes,loaded.common.begin()+PcLoginOffset),"exact synthetic login restored after reload");
        // Corrupt ciphertext: load rejects, live login cleared, replacement refused, no plaintext fallback.
        auto bad=side;bad[bad.size()/2]^=0xff;raw(f.root/"OnlineLoginData.dat",bad);
        FrontendProfiles broken;frontend_profiles_load(broken,f.dir());
        req(broken.login_error!=0,"corrupt ciphertext rejected");
        req(std::all_of(broken.common.begin()+PcLoginOffset,broken.common.begin()+PcLoginOffset+PcLoginBytes,[](auto b){return !b;}),"no plaintext fallback into live login");
        req(!frontend_profiles_save_common_4164d0(broken,f.dir()),"corrupt sidecar refuses replacement");
        req(bytes(f.root/"OnlineLoginData.dat")==bad&&bytes(f.root/"common.dat")==disk,"corrupt sidecar and common unchanged");
    }
    std::printf("login_protection_dpapi: %u checks; real DPAPI, synthetic bytes in a private temp directory only\n",checks);return 0;
}catch(const std::exception& e){frontend_profiles_set_login_protection({});std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
