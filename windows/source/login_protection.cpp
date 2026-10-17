#include "login_protection.hpp"
#include "platform/frontend_profiles.hpp"
#include <windows.h>
#include <dpapi.h>
#include <cstdio>
#include <utility>
namespace outrun::desktop {
namespace {
bool transform(bool protect,const std::uint8_t* data,std::size_t size,std::vector<std::uint8_t>& result){
    if(!size||size>platform::PcLoginCipherLimit)return false;
    DATA_BLOB input{static_cast<DWORD>(size),const_cast<BYTE*>(data)},output{};
    const bool ok=(protect?CryptProtectData(&input,L"OnlineLoginData",nullptr,nullptr,nullptr,
                                          CRYPTPROTECT_UI_FORBIDDEN,&output):
                           CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,
                                              CRYPTPROTECT_UI_FORBIDDEN,&output))!=FALSE;
    // Both DPAPI buffers (including plaintext on unprotect) are scrubbed even if
    // allocation/copy throws. No machine scope, prompts, entropy or credentials.
    struct Cleanup {DATA_BLOB& blob;~Cleanup(){if(blob.pbData){SecureZeroMemory(blob.pbData,blob.cbData);LocalFree(blob.pbData);}}} cleanup{output};
    if(!ok||!output.cbData||output.cbData>platform::PcLoginCipherLimit)return false;
    result.assign(output.pbData,output.pbData+output.cbData);return true;
}
}
void login_protection_configure(const Ini& ini,bool ordinary){
    platform::FrontendLoginProtection policy;
    policy.enabled=ordinary&&ini.flag("Misc","ProtectLoginData",false);
    if(policy.enabled){
        policy.protect=[](const auto* p,auto n,auto& out){return transform(true,p,n,out);};
        policy.unprotect=[](const auto* p,auto n,auto& out){return transform(false,p,n,out);};
    }
    platform::frontend_profiles_set_login_protection(std::move(policy));
    std::printf("login protection: %s; online transport remains incomplete\n",
                ordinary&&ini.flag("Misc","ProtectLoginData",false)?"opt-in configured (DPAPI runtime unqualified)":"off (default or excluded process)");
}
}
