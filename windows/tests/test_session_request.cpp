// The ordinary session request (source/session_request.hpp): the strict parser, and what each refusal means (a record
// request refused is ordinary play; a playback or unreadable one is never ordinary play).
#include "session_request.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace outrun::desktop::session;
namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v){std::fprintf(stderr,"session request: FAIL: %s\n",what.c_str());std::exit(1);}}
const std::uint64_t Now=2000000000;
const std::filesystem::path Game=L"E:\\Games\\OutRunDecompSmoke";
const std::string Id="0123456789abcdef0123456789abcdef";
std::string record(const std::string& extra="",const std::string& expires=std::to_string(Now+300)){
    return std::string(RequestSchema)+"\r\nrequestId="+Id+"\r\naction=record\r\nexpiresUnix="+expires+
        "\r\nresult=C:\\Users\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\results\\"+Id+
        "\r\nrecording=C:\\Users\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\recordings\\"+Id+"\r\n"+extra;
}
void refused(const std::string& bytes,Action as,const std::string& part,const std::string& what){
    const auto r=parse(bytes,Now,Game);
    check(!r.ok&&r.refused_as==as&&r.why.find(part)!=std::string::npos,what+" (why: "+r.why+")");
}
}
int main(){
    {const auto r=parse(record(),Now,Game);
     check(r.ok&&r.request.action==Action::Record&&r.request.request_id==Id&&r.request.expires_unix==Now+300,"a valid record request: "+r.why);
     check(r.request.recording.filename()==Id&&r.request.result.filename()==Id,"the folders kept");}
    {std::string lf=record();for(std::size_t at;(at=lf.find("\r\n"))!=std::string::npos;)lf.erase(at,1);
     check(parse(lf,Now,Game).ok,"LF line endings accepted");}
    // Structure: schema, size, encoding, keys.
    refused("",Action::Playback,"does not start with","an empty file is unreadable: never ordinary play");
    refused("outrun.session-request@2\r\naction=record\r\n",Action::Playback,"does not start with","another schema");
    refused(record()+std::string(4096,'x'),Action::Playback,"4 KiB","oversize");
    refused(record("note=\xC0\x80\r\n"),Action::Playback,"UTF-8","invalid UTF-8");
    refused(record("color=red\r\n"),Action::Playback,"unknown key","an unknown key, before the action is known");
    refused(record("action=record\r\n"),Action::Playback,"twice","a duplicate key");
    refused(record("just words\r\n"),Action::Playback,"key=value","a line that is not key=value");
    // The action decides what a refusal means.
    refused(std::string(RequestSchema)+"\r\nrequestId="+Id+"\r\n",Action::Playback,"no action","no action: never ordinary play");
    refused(std::string(RequestSchema)+"\r\naction=Record\r\n",Action::Playback,"neither","an action of another case");
    refused(record("",std::to_string(Now)),Action::Record,"expired","expired: a record refusal (ordinary play, no capture)");
    refused(record("",std::to_string(Now+601)),Action::Record,"10 minutes","too far ahead");
    refused(record("",""),Action::Record,"unix seconds","no expiry value");
    {auto b=record();b.replace(b.find(Id),32,"0123456789ABCDEF0123456789ABCDEF");
     refused(b,Action::Record,"requestId","an uppercase id");}
    // Paths: absolute, separate, outside the game folder.
    {auto b=record();const auto at=b.find("recording=");b.replace(at,b.find("\r\n",at)-at,"recording=relative\\folder");
     refused(b,Action::Record,"absolute","a relative recording folder");}
    {auto b=record();const auto at=b.find("recording=");b.replace(at,b.find("\r\n",at)-at,"recording=E:\\Games\\x\\..\\OutRunDecompSmoke\\rec");
     refused(b,Action::Record,"outside the game folder","a recording path that normalizes into the game folder");}
    {auto b=record();const auto at=b.find("recording=");b.replace(at,b.find("\r\n",at)-at,"recording=C:\\Users\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\results\\"+Id+"\\rec");
     refused(b,Action::Record,"separate","recording inside result");}
    {auto b=record();const auto at=b.find("result=");b.replace(at,b.find("\r\n",at)-at,"result=e:\\games\\outrundecompsmoke\\results");
     refused(b,Action::Record,"outside the game folder","result inside the game folder (any case)");}
    refused(record("tape=C:\\t.tape\r\n"),Action::Record,"a record request names no tape=","a record request naming a tape");
    // Playback: valid shape parses (the intake verifies the files; the caller decides), any defect is a playback refusal.
    const auto playback=[](const std::string& drop="",const std::string& extra=""){
        std::string keys="packageSealSha256="+std::string(64,'a')+"\r\ntape=C:\\Users\\o\\t\\export.tsv\r\ntapeSha256="+std::string(64,'b')+
            "\r\nexeSha256="+std::string(64,'c')+"\r\nstartup=C:\\Users\\o\\packages\\p1\\startup\r\nstartupSealSha256="+std::string(64,'d')+"\r\n";
        if(!drop.empty()){const auto at=keys.find(drop+"=");keys.erase(at,keys.find("\r\n",at)+2-at);}
        auto b=record(keys+extra);b.replace(b.find("action=record"),13,"action=playback");
        const auto at=b.find("recording=");   // a playback's own capture: <result>\replay (sana 37516)
        b.replace(at,b.find("\r\n",at)-at,"recording=C:\\Users\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\results\\"+Id+"\\replay");
        return b;};
    {auto b=playback();const auto at=b.find("recording=");
     b.replace(at,b.find("\r\n",at)-at,"recording=C:\\Users\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\recordings\\"+Id);
     refused(b,Action::Playback,"<result>\\replay","a playback capture outside <result>\\replay");}
    {auto b=playback();const auto at=b.find("recording=");
     b.replace(at,b.find("\r\n",at)-at,"recording=C:\\Users\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\results\\"+Id+"\\replay\\deeper");
     refused(b,Action::Playback,"<result>\\replay","a playback capture below <result>\\replay");}
    {auto b=playback();const auto at=b.find("recording=");
     b.replace(at,b.find("\r\n",at)-at,"recording=C:\\USERS\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\results\\"+Id+"\\Replay\\");
     check(parse(b,Now,Game).ok,"<result>\\replay matched as the file system does (case, a trailing separator)");}
    {const auto r=parse(playback(),Now,Game);
     check(r.ok&&r.request.action==Action::Playback&&r.request.package_seal_sha256==std::string(64,'a')&&r.request.tape_sha256==std::string(64,'b')&&
           r.request.exe_sha256==std::string(64,'c')&&r.request.startup_seal_sha256==std::string(64,'d')&&r.request.startup.filename()=="startup","a valid playback request parses: "+r.why);}
    for(const char* k:{"packageSealSha256","tape","tapeSha256","exeSha256","startup","startupSealSha256"})
        refused(playback(k),Action::Playback,k,std::string("a playback request without ")+k+"=: never ordinary play");
    {auto b=playback();b.replace(b.find("tapeSha256=")+11,64,std::string(64,'B'));refused(b,Action::Playback,"tapeSha256","an uppercase tape hash");}
    {auto b=playback();b.replace(b.find("startup=")+8,b.find("\r\n",b.find("startup="))-b.find("startup=")-8,"startup");refused(b,Action::Playback,"startup= is not an absolute","a relative startup folder");}
    {auto b=playback();const auto at=b.find("startup=")+8;b.replace(at,b.find("\r\n",at)-at,"C:\\Users\\o\\AppData\\Local\\Wheelkit\\sessions\\outrun\\results\\"+Id+"\\startup");
     refused(b,Action::Playback,"outside result=","a startup folder inside the result folder");}
    {auto b=playback();const auto at=b.find("startup=")+8;b.replace(at,b.find("\r\n",at)-at,"E:\\Games\\OutRunDecompSmoke\\startup");
     refused(b,Action::Playback,"outside the game folder","a startup folder inside the game folder");}
    for(const char* k:{"tapeSha256=","exeSha256=","startup=","startupSealSha256="}){
        const std::string v=std::string(k)==std::string("startup=")?"C:\\s":std::string(64,'e');
        refused(record(std::string(k)+v+"\r\n"),Action::Record,"a record request names no","a record request naming "+std::string(k));}
    {auto b=record("",std::to_string(Now));b.replace(b.find("action=record"),13,"action=playback");
     refused(b,Action::Playback,"expired","an expired playback request: never ordinary play");}
    check(!request_path().empty()&&request_path().filename()==L"session-request.txt","the request lives beside the fence's files");
    std::printf("session request: %d checks passed\n",checks);
    return 0;
}
