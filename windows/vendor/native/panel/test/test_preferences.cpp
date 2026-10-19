// Production preference persistence; fresh disposable files only, no window, input or force.
#include "../dbce_panel_preferences.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
namespace panel=dbce::panel;
namespace {
int checks{},guard_calls{};
bool allowed{};
bool guard(){++guard_calls;return allowed;}
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string read(const std::string& path,const char* key){
    char value[64]{};GetPrivateProfileStringA("Panel",key,"missing",value,sizeof value,path.c_str());return value;
}
}
int main(){
    fs::path root;
    try{
        root=fs::temp_directory_path()/("panel_preferences_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
        if(!fs::create_directory(root))throw std::runtime_error("Fixture directory must be fresh");
        const auto path=(root/"panel.ini").string();
        {std::ofstream file(path);file<<"[Panel]\nOwnerKey = retained\n";check(bool(file),"write disposable fixture");}
        panel::Config config;config.iniPath=path.c_str();
        check(config.canSavePreferences==nullptr,"existing consumers default to no preference guard");
        check(panel::PersistPreference(config,"Page","Controls")&&panel::PersistPreference(config,"View","Advanced")&&
              panel::PersistPreference(config,"Scale","125"),"null guard writes every existing preference");
        check(read(path,"Page")=="Controls"&&read(path,"View")=="Advanced"&&read(path,"Scale")=="125"&&read(path,"OwnerKey")=="retained",
              "null guard preserves exact Page/View/Scale values and unknown preferences");
        config.canSavePreferences=guard;allowed=false;
        check(panel::PersistPreference(config,"Page","Cameras")&&panel::PersistPreference(config,"View","Simple")&&
              panel::PersistPreference(config,"Scale","150"),"held persistence does not report a failed write");
        check(guard_calls==3&&read(path,"Page")=="Controls"&&read(path,"View")=="Advanced"&&read(path,"Scale")=="125",
              "guard holds only persistence and leaves all saved preferences unchanged");
        allowed=true;
        check(panel::PersistPreference(config,"Page","Cameras")&&panel::PersistPreference(config,"View","Simple")&&
              panel::PersistPreference(config,"Scale","150"),"preferences save after the host permits persistence again");
        check(read(path,"Page")=="Cameras"&&read(path,"View")=="Simple"&&read(path,"Scale")=="150","permitted guard writes new preferences");
        config.iniPath=nullptr;
        check(panel::PersistPreference(config,"Page","Help")&&guard_calls==6,"no-path test sessions neither write nor call the guard");
        fs::remove_all(root);std::cout<<"panel preferences: "<<checks<<" checks passed; disposable INI only\n";return 0;
    }catch(const std::exception& error){std::cerr<<"panel preferences: FAIL: "<<error.what()<<"; retained fixture "<<root.string()<<"\n";return 1;}
}
