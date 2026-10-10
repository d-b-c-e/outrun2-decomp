#pragma once
// Desktop settings (windows/): OutRunDesktop.ini next to the executable. Its
// sections and keys are those of OutRun2006Tweaks.ini, so the wheel sections of
// an existing Tweaks install can be copied over unchanged; without
// OutRunDesktop.ini, the folder's own OutRun2006Tweaks.ini is read instead.
#include <map>
#include <string>
namespace outrun::desktop {
class Ini {
public:
    // false when the file cannot be read (every value is then its fallback).
    bool load(const std::string& path);
    const std::string& path()const{return path_;}
    std::string text(const std::string& section,const std::string& key,const std::string& fallback={})const;
    int integer(const std::string& section,const std::string& key,int fallback)const;
    float number(const std::string& section,const std::string& key,float fallback)const;
    bool flag(const std::string& section,const std::string& key,bool fallback)const;
private:
    std::map<std::string,std::string> values_;   // "section/key", lower case
    std::string path_;
};
// The settings of the game folder (launch folder): OutRunDesktop.ini, else
// OutRun2006Tweaks.ini. Writes a commented OutRunDesktop.ini when neither exists.
Ini load_desktop_settings(const std::string& folder);
}
