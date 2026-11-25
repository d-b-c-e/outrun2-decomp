#pragma once
// A test-only way past a goal line (Wheelkit issue outrun-2006-0014): the goal's crowd backdrop
// (\BK\cs_BK_SUMOEND_pmt.sz, the post-goal course piece, secondary descriptor 76) loads only when the
// car crosses a goal line, which a test route holding the wheel straight cannot drive to. When armed,
// the player car is placed `before_end` segments before the end of each course piece it reaches
// (each stage and each connector between them) through the game's own recovery code (PlWrecker's
// immediate snap, 504900 phase 1), and drives the rest; the last stage's end is the goal line.
// Armed only by the desktop build in a fenced test run (goal-test.on beside the Wheelkit log); off,
// nothing here runs.
#include <cstdint>
namespace outrun::platform {
struct RaceGoalTest {
    bool armed{};
    std::int32_t before_end=40;     // segments before the piece's end the car is placed at
    std::uint32_t settle=150;       // frames on a piece (the car moving) before the placement
    bool connector=true;            // place the car on the connectors between stages too
    void (*log)(const char*){};     // the desktop's log (the Wheelkit log and runtime.log)
    // Where the car is.
    std::uint32_t piece_type=~0u,piece_key=~0u,frames{},placements{},report{};
};
inline RaceGoalTest& race_goal_test(){static RaceGoalTest t;return t;}
}
