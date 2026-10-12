// The ordinary session request (session_request.hpp).
#include "session_request.hpp"
#include "shim.hpp"
#include <windows.h>
#include <cstdlib>
#include <map>
#include <vector>
namespace outrun::desktop::session {
namespace {
bool hex(const std::string& s,std::size_t n){
    if(s.size()!=n)return false;
    for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
std::string lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
// An absolute folder or file with a drive or share root and no ".." left after normalizing; empty when not.
std::filesystem::path absolute_path(const std::string& value,std::string& why,const char* key){
    std::filesystem::path p;
    try{p=std::filesystem::u8path(value);}catch(...){why=std::string(key)+"= is not a path";return {};}
    if(value.empty()||!p.has_root_name()||!p.has_root_directory()){why=std::string(key)+"= is not an absolute path";return {};}
    p=p.lexically_normal();
    for(const auto& part:p)if(part==".."){why=std::string(key)+"= leaves its root";return {};}
    return p;
}
// a is b, or inside it (case-insensitive, as the file system is).
bool within(const std::filesystem::path& a,const std::filesystem::path& b){
    auto x=lower(utf8(a.lexically_normal().generic_string<wchar_t>())),y=lower(utf8(b.lexically_normal().generic_string<wchar_t>()));
    while(x.size()>1&&x.back()=='/')x.pop_back();
    while(y.size()>1&&y.back()=='/')y.pop_back();
    return x==y||x.rfind(y+"/",0)==0;
}
}

Parsed parse(const std::string& bytes,std::uint64_t now_unix,const std::filesystem::path& game_folder){
    Parsed r;
    r.refused_as=Action::Playback;   // until the action line is read: a request that cannot be read plays nothing
    const auto refuse=[&](const std::string& why){r.ok=false;r.why=why;return r;};
    if(bytes.size()>4096)return refuse("the session request is larger than 4 KiB");
    if(!dbce::shim::strict_utf8(bytes))return refuse("the session request is not valid UTF-8");
    std::vector<std::string> lines;
    for(std::size_t at=0;at<=bytes.size();){
        const auto nl=bytes.find('\n',at);
        auto line=bytes.substr(at,nl==std::string::npos?std::string::npos:nl-at);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        lines.push_back(line);
        if(nl==std::string::npos)break;
        at=nl+1;
    }
    while(!lines.empty()&&lines.back().empty())lines.pop_back();
    if(lines.empty()||lines[0]!=RequestSchema)return refuse(std::string("the session request does not start with ")+RequestSchema);
    std::map<std::string,std::string> kv;
    static const char* const Known[]{"requestId","action","expiresUnix","result","recording","packageSealSha256","tape"};
    for(std::size_t i=1;i<lines.size();++i){
        const auto& line=lines[i];
        const auto eq=line.find('=');
        if(eq==std::string::npos||eq==0)return refuse("session request line "+std::to_string(i+1)+" is not key=value");
        const auto key=line.substr(0,eq),value=line.substr(eq+1);
        bool known=false;for(const char* k:Known)known=known||key==k;
        if(!known)return refuse("the session request has an unknown key \""+key.substr(0,32)+"\"");
        if(!kv.emplace(key,value).second)return refuse("the session request names "+key+"= twice");
    }
    // The action first, so that every later refusal knows what it refuses.
    const auto a=kv.find("action");
    if(a==kv.end())return refuse("the session request names no action=");
    if(a->second=="record")r.refused_as=Action::Record;
    else if(a->second=="playback")r.refused_as=Action::Playback;
    else return refuse("action= is neither record nor playback");
    Request q;q.action=r.refused_as;
    const auto id=kv.find("requestId");
    if(id==kv.end()||!hex(id->second,32))return refuse("requestId= is not 32 lowercase hex digits");
    q.request_id=id->second;
    const auto ex=kv.find("expiresUnix");
    if(ex==kv.end()||ex->second.empty()||ex->second.size()>12||ex->second.find_first_not_of("0123456789")!=std::string::npos)
        return refuse("expiresUnix= is not unix seconds");
    q.expires_unix=std::strtoull(ex->second.c_str(),nullptr,10);
    if(q.expires_unix<=now_unix)return refuse("the session request has expired");
    if(q.expires_unix-now_unix>600)return refuse("the session request expires more than 10 minutes from now");
    std::string why;
    const auto res=kv.find("result");
    if(res==kv.end())return refuse("the session request names no result=");
    q.result=absolute_path(res->second,why,"result");if(q.result.empty())return refuse(why);
    const auto rec=kv.find("recording");
    if(rec==kv.end())return refuse("the session request names no recording=");
    q.recording=absolute_path(rec->second,why,"recording");if(q.recording.empty())return refuse(why);
    if(within(q.result,q.recording)||within(q.recording,q.result))return refuse("result= and recording= must be separate folders, neither inside the other");
    if(!game_folder.empty()&&(within(q.result,game_folder)||within(q.recording,game_folder)||within(game_folder,q.result)||within(game_folder,q.recording)))
        return refuse("result= and recording= must be outside the game folder");
    const auto seal=kv.find("packageSealSha256"),tape=kv.find("tape");
    if(q.action==Action::Record){
        if(seal!=kv.end()||tape!=kv.end())return refuse("a record request names no packageSealSha256= or tape=");
    }else{
        if(seal==kv.end()||!hex(seal->second,64))return refuse("packageSealSha256= is not 64 lowercase hex digits");
        q.package_seal_sha256=seal->second;
        if(tape==kv.end())return refuse("the playback request names no tape=");
        q.tape=absolute_path(tape->second,why,"tape");if(q.tape.empty())return refuse(why);
    }
    r.ok=true;r.request=q;r.why.clear();
    return r;
}

std::filesystem::path request_path(){
    wchar_t buf[32768];
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buf,32768);
    if(!n||n>=32768)return {};
    return std::filesystem::path(std::wstring(buf,n))/L"dbce"/L"outrun2006-decomp"/L"session-request.txt";
}
}
