#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
namespace outrun::desktop {
// Native slots: bumper 0, in-car 1, chase 2. Logical Bonnet 3 uses slot 0 with
// a model-derived render transform; native cinematic slots >=3 stay distinct.
struct CameraFraming {
    std::array<bool,4> realistic{{true,true,false,false}};
    std::array<std::array<float,2>,4> offsets{}; // vertical degrees, Game / Realistic, shared across cars
    static bool tunable(unsigned view){return view<4;}
    static const char* prefix(unsigned view){return view==0?"Bumper":view==1?"Cockpit":view==2?"Chase":"Bonnet";}
    static std::string set_key(unsigned view){return std::string(prefix(view))+"Realistic";}
    static std::string offset_key(unsigned view,bool seat){return std::string(prefix(view))+(seat?"RealisticFovOffset":"GameFovOffset");}
    bool seat(unsigned view,bool triples)const{return tunable(view)&&triples&&realistic[view];}
    float fov(unsigned view,float game,float physical,bool triples)const{
        if(!tunable(view))return game;
        const bool r=seat(view,triples);
        const float base=r?physical:game,offset=offsets[view][r?1:0];
        if(!std::isfinite(base)||!std::isfinite(offset)||!(base>0.f&&base<3.14159265f))return game;
        // The player's offset is unrestricted; only the resulting perspective must stay finite and nondegenerate.
        return float(std::clamp(double(base)+double(offset)*0.017453292519943295,0.001,3.140592653589793));
    }
};
struct CameraPoseOffset {float side{},up{},forward{},pitch{};};
inline std::string camera_pose_key(unsigned model,unsigned view,bool seat,const char* axis){
    return "Car"+std::to_string(model)+"."+CameraFraming::prefix(view)+(seat?"Realistic":"Game")+axis+"Offset";
}
}
