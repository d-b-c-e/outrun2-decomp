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
    refused(record("tape=C:\\t.tape\r\n"),Action::Record,"no packageSealSha256","a record request naming a tape");
    // Playback: valid shape parses (this build refuses to play it; the caller decides), any defect is a playback refusal.
    {auto b=record("packageSealSha256="+std::string(64,'a')+"\r\ntape=C:\\Users\\o\\t\\export.tsv\r\n");b.replace(b.find("action=record"),13,"action=playback");
     const auto r=parse(b,Now,Game);check(r.ok&&r.request.action==Action::Playback&&r.request.package_seal_sha256.size()==64,"a valid playback request parses: "+r.why);}
    {auto b=record("tape=C:\\Users\\o\\t\\export.tsv\r\n");b.replace(b.find("action=record"),13,"action=playback");
     refused(b,Action::Playback,"packageSealSha256","a playback request without its seal: never ordinary play");}
    {auto b=record("",std::to_string(Now));b.replace(b.find("action=record"),13,"action=playback");
     refused(b,Action::Playback,"expired","an expired playback request: never ordinary play");}
    check(!request_path().empty()&&request_path().filename()==L"session-request.txt","the request lives beside the fence's files");
    std::printf("session request: %d checks passed\n",checks);
    return 0;
}
