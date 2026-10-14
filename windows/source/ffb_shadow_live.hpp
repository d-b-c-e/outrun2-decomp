#pragma once
#include "ffb_player_model.hpp"
#include <string>
namespace outrun::desktop {
// The selected model and optional trace share one car observer and one producer stream.
bool ffb_shadow_select(const ffb::player::Selection& selection,ffb::shadow::Core::RequestSink sink);
void ffb_shadow_invalidate_selected() noexcept;
// Returns false when absent/refused/open failed. Never selects wheel output.
bool ffb_shadow_configure(const std::wstring& path,bool ordinary_allowed);
void ffb_shadow_wrap_input_filter();
void ffb_shadow_shutdown();
}
