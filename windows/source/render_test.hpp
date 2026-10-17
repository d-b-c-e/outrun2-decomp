#pragma once
// An opt-in rendering test for a latched (test) process (sana 215675; windows/docs/TRIPLE-SCREENS.md, "Rendering tests").
// %LOCALAPPDATA%\dbce\outrun2006-decomp\render-test.txt, beside inject.on, is read once by a latched process right after
// the fence prepared its run, and by no other process. Absent: the test process is exactly as before (Screens Off,
// Planar, no frames). Present, it must parse strictly and name this session's nonce, else the run is refused before the
// game initializes (the fence's refusal note):
//   outrun.render-test@1
//   nonce=<inject.on's nonce>
//   screens=off|surround|separate   this run's [Triple] Screens (default off)
//   views=planar|angled             this run's [Triple] Views (default planar; angled needs screens other than off)
//   frames=<0..300>                 frames written to <run>\frames as frame-NNNN.ppm (default 0: none)
//   every=<1..60>                   presented frames between two of them (default 1)
//   after=<0..36000>                presented frames after the race entry (mode 16 or 18) before the first (default 0)
//   scale=1|2|4                     box downscale of each frame (default 2)
// At most 1 KiB, one key per line, each at most once, no other key. Only presentation changes: the process stays latched
// (home in the run folder, the game folder only read, OR2_ switches cleared, offline, no force output), the rig is the
// fixture's with the eye matched to the game's FOV, and [Window] keeps the test defaults. The bytes are kept as
// <run>\render-test.txt. Input stays the test's own (inject.txt); frames are the game's own back buffer.
#include "triple_screens.hpp"
#include <string>
namespace outrun::desktop {
struct RenderTest {
    bool requested{};
    TripleScreens screens{TripleScreens::off};
    TripleProjection views{TripleProjection::planar};
    unsigned frames{},every{1},after{},scale{2};
};
inline constexpr const char* RenderTestSchema="outrun.render-test@1";
// Pure. `nonce` is the session's (empty: refused). `out.requested` is set on success.
bool parse_render_test(const std::string& bytes,const std::string& nonce,RenderTest& out,std::string& why);
// The fence step in a latched process. No render-test.txt: true, not requested. Otherwise it is read (no link on its path,
// a plain file), parsed, its bytes written to <run_root>\render-test.txt (a new file) and, with frames, <run_root>\frames
// made (a new folder). false: `why` refuses the run.
bool load_render_test(const std::wstring& dbce_folder,const std::wstring& run_root,const std::string& nonce,RenderTest& out,
                      std::string& why);
// Applies a requested test's capture: the frame burst into <run_root>\frames, armed by the first race input sample.
// Call after the input filter is chosen. Nothing without frames.
void render_test_start_capture(const RenderTest& test,const std::wstring& run_root);
}
