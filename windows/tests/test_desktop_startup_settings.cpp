// Actual startup defaults handler with disposable files; no game, device or owner settings.
#include "desktop_settings.hpp"
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace outrun::desktop;
namespace {
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
std::string read(const std::string& path){std::ifstream f(path,std::ios::binary);std::ostringstream out;out<<f.rdbuf();return out.str();}
void write(const std::string& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f<<bytes;}
bool no_temp(const std::string& path){WIN32_FIND_DATAA data{};const HANDLE h=FindFirstFileA((path+".tmp-*").c_str(),&data);if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;FindClose(h);return false;}
struct Publisher {SettingsPublish old=settings_create_publish();~Publisher(){settings_create_publish()=old;}};
SettingsPublish real_create=nullptr;
bool create_rival(const std::string& temp,const std::string& path){write(path,"; concurrent owner\r\n[FFB]\nEnabled = false");return real_create(temp,path);}
void run(){
    char temp[MAX_PATH];check(GetTempPathA(MAX_PATH,temp)!=0,"temp directory unavailable");
    const std::string folder=std::string(temp)+"or2_startup_settings_"+std::to_string(GetCurrentProcessId());
    check(CreateDirectoryA(folder.c_str(),nullptr)!=0,"fresh fixture directory required");
    const auto path=folder+"/OutRunDesktop.ini",legacy=folder+"/OutRun2006Tweaks.ini";
    auto ini=load_desktop_settings(folder,false);
    check(!ini.has_section("Controls")&&GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES,"read-only startup created defaults");
    ini=load_desktop_settings(folder);
    check(ini.integer("Controls","Schema",0)==1&&ini.integer("Window","FrameLatency",0)==2,"fresh defaults missing");
    check(no_temp(path),"successful creation leaked temporary file");
    const std::string owner="\xef\xbb\xbf; owner\r\n[Controls]\nSchema = 2\r\n[FFB]\nEnabled = false";
    write(path,owner);WIN32_FILE_ATTRIBUTE_DATA before{},after{};
    check(GetFileAttributesExA(path.c_str(),GetFileExInfoStandard,&before)!=0,"owner metadata unavailable");
    ini=load_desktop_settings(folder);
    check(ini.integer("Controls","Schema",0)==2&&read(path)==owner,"readable owner settings changed");
    check(GetFileAttributesExA(path.c_str(),GetFileExInfoStandard,&after)!=0&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0&&CompareFileTime(&before.ftCreationTime,&after.ftCreationTime)==0&&before.dwFileAttributes==after.dwFileAttributes,"readable owner metadata changed");
    // Deny reads but allow writes: the old load-failure -> fopen(wb) path could truncate this file.
    const HANDLE held=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    check(held!=INVALID_HANDLE_VALUE,"could not retain unreadable/write-shared file");
    ini=load_desktop_settings(folder);CloseHandle(held);
    check(!ini.has_section("Controls")&&read(path)==owner,"unreadable existing file overwritten");
    check(no_temp(path),"unreadable file created a temporary");
    check(DeleteFileA(path.c_str())!=0,"remove own fixture file");
    write(legacy,"[DirectInput]\nSteeringAxis = 0\n");ini=load_desktop_settings(folder);
    check(!ini.has_section("Controls")&&ini.has_section("Wheel"),"legacy import header changed");
    check(DeleteFileA(path.c_str())!=0&&DeleteFileA(legacy.c_str())!=0,"remove own legacy fixtures");
    {Publisher reset;settings_create_publish()=[](const std::string&,const std::string&){SetLastError(ERROR_ACCESS_DENIED);return false;};
     ini=load_desktop_settings(folder);}
    check(!ini.has_section("Controls")&&GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES&&no_temp(path),"failed publication left a partial default or temp");
    {Publisher reset;real_create=reset.old;settings_create_publish()=create_rival;ini=load_desktop_settings(folder);}
    check(read(path)=="; concurrent owner\r\n[FFB]\nEnabled = false"&&!ini.has_section("Controls")&&no_temp(path),"concurrent creator was overwritten or temp leaked");
    check(DeleteFileA(path.c_str())!=0&&CreateDirectoryA(path.c_str(),nullptr)!=0,"create directory sentinel");
    ini=load_desktop_settings(folder);
    check((GetFileAttributesA(path.c_str())&FILE_ATTRIBUTE_DIRECTORY)!=0&&no_temp(path),"directory sentinel changed");
    check(RemoveDirectoryA(path.c_str())!=0&&RemoveDirectoryA(folder.c_str())!=0,"fixture cleanup failed");
}
}
int main(){try{run();}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}std::cout<<"startup settings: "<<checks<<" checks passed\n";}
