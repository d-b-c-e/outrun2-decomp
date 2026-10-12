// A recording's startup snapshot (source/startup_state.hpp; sana 37135/37176): the seal and its inventory, the members'
// bytes, nothing unlisted, the private home's seed, and the playback settings.
#include "startup_state.hpp"
#include "desktop_settings.hpp"
#include "sha256.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
using namespace outrun::desktop::startup;
namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v){std::fprintf(stderr,"startup state: FAIL: %s\n",what.c_str());std::exit(1);}}
const std::string Exe(64,'a'),Id="0123456789abcdef0123456789abcdef";
std::string sha(const std::string& s){return dbce::shim::hex(dbce::shim::sha256(s));}
void put(const std::filesystem::path& p,const std::string& bytes){
    std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<bytes;}
std::string get(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
// A snapshot folder from relative path -> bytes (absent: the names listed absent); returns the seal's sha256.
std::string snapshot(const std::filesystem::path& dir,const std::map<std::string,std::string>& files,const std::vector<std::string>& absent,
                     const std::string& exe=Exe,const std::string& extra_lines=""){
    std::filesystem::create_directories(dir);
    std::string seal=std::string(Schema)+"\nrequestId\t"+Id+"\nexeSha256\t"+exe+"\n";
    for(const auto& [rel,bytes]:files){auto w=std::filesystem::u8path(rel);put(dir/w.make_preferred(),bytes);
        seal+="file\t"+rel+"\t"+std::to_string(bytes.size())+"\t"+sha(bytes)+"\n";}
    for(const auto& a:absent)seal+="absent\t"+a+"\n";
    seal+=extra_lines;
    put(dir/L"complete.tsv",seal);
    return sha(seal);
}
std::filesystem::path temp_dir(){
    wchar_t t[MAX_PATH];GetTempPathW(MAX_PATH,t);
    auto d=std::filesystem::path(t)/(L"startup-state-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(d);return d;
}
const std::map<std::string,std::string> Files{{"SaveGame/license.sav",std::string("LIC\0\1\2",6)},{"SaveGame/records/best.bin","best"},
                                              {"options.ini","[options]\nsound=1\n"},{"OutRunDesktop.ini","[Controls]\na=1\n[Window]\nFrameLatency=0\n"}};
}
int main(){
    const auto root=temp_dir();
    // The inventory.
    check(admitted("options.ini")&&admitted("OutRunDesktop.ini")&&admitted("SaveGame/a.sav")&&admitted("SaveGame/1/2/3/4/f.bin"),"admitted paths");
    for(const char* bad:{"SaveGame/1/2/3/4/5/f.bin","other.txt","SaveGame","SaveGame/../x","SaveGame/a:b","SaveGame/con.txt","SaveGame/x.","SaveGame//x",
                         "savegame/a.sav","SaveGame/a\\b","Options.ini","SaveGame/a*"})
        check(!admitted(bad),std::string("refused path: ")+bad);
    // A valid snapshot verifies; its members are listed.
    const auto good=root/L"good";const auto seal=snapshot(good,Files,{});
    {const auto v=verify(good,seal,Exe);
     check(v.ok&&v.snapshot.request_id==Id&&v.snapshot.exe_sha256==Exe&&v.snapshot.members.size()==4,"a valid snapshot verifies: "+v.why);}
    check(!verify(good,std::string(64,'0'),Exe).ok,"another seal hash is refused");
    check(verify(good,seal,std::string(64,'b')).why.find("another exe")!=std::string::npos,"a snapshot of another exe is refused");
    {const auto d=root/L"absent";const auto s=snapshot(d,{},{"options.ini","OutRunDesktop.ini","SaveGame"});
     const auto v=verify(d,s,Exe);check(v.ok&&v.snapshot.members.size()==3,"an empty start (everything absent) verifies: "+v.why);}
    // Tampering after the seal.
    {const auto d=root/L"changed";const auto s=snapshot(d,Files,{});put(d/L"SaveGame"/L"license.sav",std::string("LIC\0\1\3",6));
     check(verify(d,s,Exe).why.find("differs")!=std::string::npos,"a member changed after the seal is refused");}
    {const auto d=root/L"extra";const auto s=snapshot(d,Files,{});put(d/L"SaveGame"/L"planted.sav","x");
     check(verify(d,s,Exe).why.find("unlisted")!=std::string::npos,"an unlisted file is refused");}
    {const auto d=root/L"missing";const auto s=snapshot(d,Files,{});DeleteFileW((d/L"options.ini").c_str());
     check(verify(d,s,Exe).why.find("lacks")!=std::string::npos,"a missing member is refused");}
    // The seal's own shape.
    const auto refused=[&](const std::wstring& name,const std::map<std::string,std::string>& files,const std::vector<std::string>& absent,
                           const std::string& extra,const std::string& part,const std::string& what){
        const auto d=root/name;const auto s=snapshot(d,files,absent,Exe,extra);const auto v=verify(d,s,Exe);
        check(!v.ok&&v.why.find(part)!=std::string::npos,what+" (why: "+v.why+")");};
    refused(L"dup",Files,{"options.ini"},"","twice","options.ini listed as file and absent");
    refused(L"sgboth",Files,{"SaveGame"},"","absent and SaveGame files","SaveGame absent with SaveGame files");
    refused(L"noopt",{{"OutRunDesktop.ini","x"}},{},"","whether options.ini","options.ini not accounted for");
    refused(L"foreign",Files,{},"file\tother.txt\t1\t"+std::string(64,'c')+"\n","not in the startup inventory","a member outside the inventory");
    refused(L"badline",Files,{},"note\tsomething\n","neither","an unknown line kind");
    refused(L"badabsent",Files,{},"absent\tSaveGame/a.sav\n","can be absent","only the three names can be absent");
    {const auto d=root/L"schema";snapshot(d,Files,{});auto t=get(d/L"complete.tsv");t.replace(0,std::string(Schema).size(),"outrun.startup-state@2");put(d/L"complete.tsv",t);
     check(verify(d,sha(t),Exe).why.find("does not start with")!=std::string::npos,"another schema is refused");}
    {const auto d=root/L"badid";snapshot(d,Files,{});auto t=get(d/L"complete.tsv");t.replace(t.find(Id),32,std::string(32,'G'));put(d/L"complete.tsv",t);
     check(verify(d,sha(t),Exe).why.find("requestId")!=std::string::npos,"a malformed request id is refused");}
    {const auto d=root/L"badsize";snapshot(d,Files,{});auto t=get(d/L"complete.tsv");const auto at=t.find("\t6\t");t.replace(at,3,"\t6x\t");put(d/L"complete.tsv",t);
     check(verify(d,sha(t),Exe).why.find("size")!=std::string::npos,"a malformed size is refused");}
    check(!parse(std::string(Schema)+"\nrequestId\t"+Id+"\n").ok,"a seal without the exe line is refused");
    // The seed: a new home with SaveGame and options.ini, never the settings file; never over an existing home.
    {const auto v=verify(good,seal,Exe);const auto home=root/L"home";
     check(seed(good,v.snapshot,home).empty(),"seeded");
     check(get(home/L"SaveGame"/L"license.sav")==Files.at("SaveGame/license.sav")&&get(home/L"SaveGame"/L"records"/L"best.bin")=="best"&&
           get(home/L"options.ini")==Files.at("options.ini"),"the SaveGame members and options.ini, byte for byte");
     check(GetFileAttributesW((home/L"OutRunDesktop.ini").c_str())==INVALID_FILE_ATTRIBUTES,"the settings file is not seeded into home");
     check(!seed(good,v.snapshot,home).empty(),"an existing home is refused");}
    {const auto d=root/L"absent2";const auto s=snapshot(d,{},{"options.ini","OutRunDesktop.ini","SaveGame"});const auto v=verify(d,s,Exe);
     const auto home=root/L"home-empty";check(seed(d,v.snapshot,home).empty()&&GetFileAttributesW((home/L"SaveGame").c_str())!=INVALID_FILE_ATTRIBUTES&&
           GetFileAttributesW((home/L"options.ini").c_str())==INVALID_FILE_ATTRIBUTES,"an empty start seeds an empty SaveGame and no options.ini");}
    // The playback settings: the recording's, with the owner's [Window] and [Triple].
    {const std::string recorded="[Controls]\nsteer = x\n\n[Window]\nFrameLatency = 0\n\n[Triple]\nScreens = Off\n\n[Graphics]\nDrawDistanceIncrease = 64\n";
     const std::string owner="[Window]\r\nFrameLatency = 2\r\nWindowedBorderless = true\r\n[Graphics]\r\nDrawDistanceIncrease = 1024\r\n[ triple ]\r\nScreens = Separate monitors\r\n";
     const auto m=playback_settings(recorded,owner);
     outrun::desktop::Ini ini;ini.load_text(m,"merged");
     check(ini.text("Controls","steer")=="x"&&ini.text("Graphics","DrawDistanceIncrease")=="64","the recording's sections kept: "+m);
     check(ini.text("Window","FrameLatency")=="2"&&ini.text("Window","WindowedBorderless")=="true"&&ini.text("Triple","Screens")=="Separate monitors",
           "the owner's [Window] and [Triple]: "+m);
     check(m.find("FrameLatency = 0")==std::string::npos&&m.find("Screens = Off")==std::string::npos&&m.find("1024")==std::string::npos,
           "neither the recording's presentation nor the owner's other sections: "+m);
     outrun::desktop::Ini none;none.load_text(playback_settings(recorded,""),"none");
     check(!none.has_section("Window")&&!none.has_section("Triple")&&none.text("Controls","steer")=="x","no owner presentation: none applied");}
    std::error_code ec;std::filesystem::remove_all(root,ec);
    std::printf("startup state: %d checks passed\n",checks);
    return 0;
}
