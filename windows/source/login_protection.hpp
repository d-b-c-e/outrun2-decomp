#pragma once
#include "desktop_settings.hpp"
namespace outrun::desktop {
// Ordinary play only. Installs callbacks; configuration performs no crypto/I/O.
void login_protection_configure(const Ini&,bool ordinary);
}
