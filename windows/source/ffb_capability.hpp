#pragma once
// Build-owned force capability: native admission and the package consumer use this same policy.
// Installing a newer external package generator cannot grant a pre-policy executable this marker.
#include "ffb_player_model.hpp"
#include <array>
#include <string>
#include <string_view>

namespace outrun::desktop::ffb::capability {
inline constexpr std::array<const char*,2> Models{player::LegacyId,producer2::ModelId};
inline constexpr std::array<const char*,1> WheelOutput{producer2::ModelId};
inline constexpr const char* Legacy="trace-only";
constexpr std::string_view model_id(player::Model model){
    return model==player::Model::Legacy?player::LegacyId:model==player::Model::Producer2?producer2::ModelId:"";
}
constexpr bool wheel_output(player::Model model){
    for(const auto* id:WheelOutput)if(model_id(model)==id)return true;
    return false;
}
static_assert(std::string_view(Models[0])=="outrun-legacy@1"&&std::string_view(Models[1])=="outrun-producer@2");
static_assert(!wheel_output(player::Model::Legacy)&&wheel_output(player::Model::Producer2)&&!wheel_output(player::Model::Refused));
// Fixed public IDs need no JSON escaping. Arrays and order come from the admission policy above.
inline std::string json(){
    std::string out="{\"models\": [";
    for(std::size_t i=0;i<Models.size();++i)out+=std::string(i?",":"")+"\""+Models[i]+"\"";
    out+="], \"wheelOutput\": [";
    for(std::size_t i=0;i<WheelOutput.size();++i)out+=std::string(i?",":"")+"\""+WheelOutput[i]+"\"";
    return out+"], \"legacy\": \""+Legacy+"\"}";
}
}
