// DrawDistanceIncrease / DrawDistanceBehind (race_draw_distance.hpp).
#include "platform/race_draw_distance.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
namespace outrun::platform {
DrawDistanceSettings& draw_distance(){static DrawDistanceSettings s;return s;}
namespace {
std::string trim(const std::string& s){
    const auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
// stol(text, nullptr, 0) as Tweaks reads keys and values; false when nothing numeric is there.
bool integer(const std::string& text,long& out){
    if(text.empty())return false;
    char* end=nullptr;const long v=std::strtol(text.c_str(),&end,0);
    if(end==text.c_str())return false;
    out=v;return true;
}
}
std::size_t draw_distance_read_exclusions(const std::string& text,DrawDistanceSettings& s){
    std::istringstream in(text);std::string line;int stage=-1;std::size_t nodes=0;
    while(std::getline(in,line)){
        if(const auto hash=line.find_first_of("#;");hash!=std::string::npos)line.resize(hash);
        line=trim(line);if(line.empty())continue;
        if(line.front()=='['){
            const auto close=line.find(']');const auto name=line.substr(1,close==std::string::npos?std::string::npos:close-1);
            const auto digit=name.find_first_of("0123456789");
            stage=digit==std::string::npos?-1:std::atoi(name.c_str()+digit);
            if(stage<0||stage>=128)stage=-1;   // Tweaks: an invalid stage section is skipped
            continue;
        }
        if(stage<0)continue;
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        const auto key=trim(line.substr(0,eq));
        long object;
        if(key=="SkipQuickSort"||!integer(key,object)||object<0||object>=256)continue;
        std::istringstream values(line.substr(eq+1));std::string token;
        while(std::getline(values,token,',')){
            long node;
            if(!integer(trim(token),node)||node<0||node>=16384)continue;
            s.exclusions[{stage,int(object)}].set(std::size_t(node));++nodes;
        }
    }
    return nodes;
}
int draw_distance_ahead(const DrawDistanceSettings& s,int stage,int section){
    return stage==20&&section<30?std::min(s.increase,80):s.increase;   // STAGE_GRAND_CANYON
}
}
