#pragma once
#include <string>
namespace outrun::desktop {
// Returns false when absent/refused/open failed. Never selects wheel output.
bool ffb_shadow_configure(const std::wstring& path,bool ordinary_allowed);
void ffb_shadow_wrap_input_filter();
void ffb_shadow_shutdown();
}
