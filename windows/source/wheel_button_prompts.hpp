#pragma once
// Display only: the startup profile joined to the backend's accepted tick, never an editor draft or a device reader.
#include "../shim/shim.hpp"
#include <string>
#include <vector>

namespace outrun::desktop::prompts {
std::string label(const dbce::controls::Binding&);
std::vector<std::string> contextual(const dbce::controls::Section& active,const dbce::shim::Frame&,
    const std::vector<dbce::shim::DeviceReport>&,std::uint32_t mode,bool car);
}
