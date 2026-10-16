#pragma once
namespace outrun::desktop {
// Configure once after the backend, then wrap before the F6 neutral-input owner.
// Tests/playback never read a live keyboard; consumed playback supplies its row.
void level_select_input_configure(bool enabled,bool test_or_playback);
void level_select_wrap_input_filter();
}
