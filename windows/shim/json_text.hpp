#pragma once
// Strict JSON text for the shim's reports and the control journal. Moved unchanged from host/control_journal.hpp
// (journal_detail, which keeps it under its old name), so the ordinary DLL path can write its device and profile JSON
// without the journal: the journal's observer transport is qualified for x64 only, and the x86 build leaves it out
// (shim_dll/README.md, "x86 build: ordinary only").
#include "shim.hpp"
#include <cstddef>
#include <string>

namespace dbce::shim {
// Strict JSON string: valid UTF-8 only; quote, backslash and controls escaped; refused above `max` escaped bytes.
inline bool json_string(std::string& out,const std::string& s,std::size_t max){
    if(!strict_utf8(s))return false;
    static constexpr char hexdigits[]="0123456789abcdef";
    std::string e;e.reserve(s.size()+2);e+='"';
    for(const unsigned char c:s){
        if(c=='"')e+="\\\"";
        else if(c=='\\')e+="\\\\";
        else if(c<0x20||c==0x7f){e+="\\u00";e+=hexdigits[c>>4];e+=hexdigits[c&15];}
        else e+=char(c);
        if(e.size()>max+1)return false;
    }
    e+='"';out+=e;return true;
}
}
