#pragma once
// Desktop settings (windows/): OutRunDesktop.ini next to the executable.
// [Controls] holds the canonical control bindings in the wheel toolkit's
// ini-controls-1 form (controls_store.hpp), which Wheelkit edits; [Wheel] and
// [Window] the build's own options. The OutRun2006Tweaks [DirectInput] keys are
// read only by the one-time import into [Controls].
#include <map>
#include <string>
#include <vector>
namespace outrun::desktop {
class Ini {
public:
    // false when the file cannot be read (every value is then its fallback).
    bool load(const std::string& path);
    const std::string& path()const{return path_;}
    bool has_section(const std::string& section)const;
    // The section's lines as written (keys keep their case), for parsers of their own.
    std::vector<std::string> section_lines(const std::string& section)const;
    std::string text(const std::string& section,const std::string& key,const std::string& fallback={})const;
    int integer(const std::string& section,const std::string& key,int fallback)const;
    float number(const std::string& section,const std::string& key,float fallback)const;
    bool flag(const std::string& section,const std::string& key,bool fallback)const;
private:
    std::map<std::string,std::string> values_;                 // "section/key", lower case
    std::map<std::string,std::vector<std::string>> sections_;  // lower-case section -> raw lines
    std::string path_;
};
// Replaces [section]'s body in the file (or appends the section); every other byte of the file is kept.
bool write_section(const std::string& path,const std::string& section,const std::vector<std::string>& body,std::string& error);
// OutRunDesktop.ini in `folder`, written with comments when it does not exist. When it is new and the folder has
// an OutRun2006Tweaks.ini, the new file is left without [Controls] so that the Tweaks bindings get imported.
Ini load_desktop_settings(const std::string& folder);
}
