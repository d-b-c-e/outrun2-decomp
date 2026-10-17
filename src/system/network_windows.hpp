#pragma once
#include "platform/pc_network.hpp"
namespace outrun::platform {
// Lazy Winsock supplier: constructing it performs no initialization, lookup,
// enumeration or socket operation. Desktop installs only with explicit override.
PcNetworkPlatform pc_network_windows_platform();
}
