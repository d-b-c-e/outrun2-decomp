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
    // The same parse of text already read (a playback process's merged settings, startup_state.hpp); `path` names it.
    void load_text(const std::string& text,const std::string& path);
    const std::string& path()const{return path_;}
    bool has_section(const std::string& section)const;
    // The section's lines as written (keys keep their case), for parsers of their own.
    std::vector<std::string> section_lines(const std::string& section)const;
    std::string text(const std::string& section,const std::string& key,const std::string& fallback={})const;
    bool has_value(const std::string& section,const std::string& key)const;
    int integer(const std::string& section,const std::string& key,int fallback)const;
    float number(const std::string& section,const std::string& key,float fallback)const;
    bool flag(const std::string& section,const std::string& key,bool fallback)const;
private:
    std::map<std::string,std::string> values_;                 // "section/key", lower case
    std::map<std::string,std::vector<std::string>> sections_;  // lower-case section -> raw lines
    std::string path_;
};
// [Wheel] steering as both input paths read it (wheel_input.cpp, and the shim backend's outrun_adapter Steering):
// sensitivity (default 1) and dead zone (default 0, clamped to 0..0.9). strtof accepts "nan" and "inf"; a non-finite
// value is the default, so neither path clamps or rounds a NaN (romi 25900). Finite values are unchanged.
struct WheelSteering { float sensitivity{1.f},dead_zone{0.f}; };
WheelSteering wheel_steering(float sensitivity,float dead_zone);
WheelSteering wheel_steering(const Ini& settings);   // [Wheel] SteeringSensitivity / SteeringDeadZone
// Replaces [section]'s body in the file (or appends the section); every other byte of the file is kept. The file is
// read once and whole (a missing file is empty; one that cannot be read is refused) and replaced in one step (a flushed
// temp file moved over it): on any failure the original is left exactly as it was and `error` says why.
bool write_section(const std::string& path,const std::string& section,const std::vector<std::string>& body,std::string& error);
// The publish step of write_section / write_key: the flushed temp file moved over the original (MoveFileExA
// REPLACE_EXISTING | WRITE_THROUGH by default). A test seam: a failing one shows the original survives a refused publish.
using SettingsPublish=bool(*)(const std::string& temp,const std::string& path);
SettingsPublish& settings_publish();
// One key of [section] set to `value` in the file as it is now (re-read, so another writer's lines, Wheelkit's
// [Controls] among them, are kept): every line of that key (any case) becomes "key = value", or the line is added;
// the section's other lines, comments included, and the rest of the file are kept (write_section).
bool write_key(const std::string& path,const std::string& section,const std::string& key,const std::string& value,std::string& error);
// OutRunDesktop.ini in `folder`, written with comments when it does not exist. When it is new and the folder has
// an OutRun2006Tweaks.ini, the new file is left without [Controls] so that the Tweaks bindings get imported.
// create_if_missing=false (a latched test process): read only; a missing file is simply empty.
Ini load_desktop_settings(const std::string& folder,bool create_if_missing=true);
}
