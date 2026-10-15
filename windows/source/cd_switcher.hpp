#pragma once
namespace outrun::desktop {
class Ini;
void cd_switcher_configure(const Ini&,bool test_or_playback,bool recording);
void cd_switcher_wrap_input_filter();
}
