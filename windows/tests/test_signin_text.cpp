// HideOnlineSigninText and ShowOutRunMilesOnMenu (src/platform/pc_screen.hpp): the 443110 calls the 443256 jump skips,
// the help-bar caption the 446510 / 446A80 / 446B20 detours leave out, and 4413E2's status line. No game, no assets.
#include "platform/pc_screen.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"signin text: FAIL: %s\n",what);std::exit(1);}}
}
int main(){
    using namespace outrun::platform;
    check(!g_pc_hide_online_signin_text,"HideOnlineSigninText off by default, as Tweaks' INI");
    check(g_pc_show_outrun_miles_on_menu,"ShowOutRunMilesOnMenu on by default, as Tweaks");
    // 443256: only the 4400D3 box's open (44327C) and commit (443286); 443110's other opens stay.
    check(pc_signin_box_call_skipped(true,0x44327cu)&&pc_signin_box_call_skipped(true,0x443286u),"the box's two calls skipped");
    const unsigned others[]{0x443168u,0x44316fu,0x443184u,0x443191u,0x4431e6u,0x4431edu,0x443243u,0x44324au};
    bool kept=true;for(unsigned s:others)kept=kept&&!pc_signin_box_call_skipped(true,s);
    check(kept,"443110's other call sites unchanged");
    check(!pc_signin_box_call_skipped(false,0x44327cu)&&!pc_signin_box_call_skipped(false,0x443286u),"without the fix: the original");
    check(pc_hidden_signin_caption(true)==664u&&pc_hidden_signin_caption(false)==0xffffffffu,"the caption 664 (SIGN-IN), none without");
    // 4413E2: only the not signed in branch drawing string 0x474; 4413CE (signed in, 0x476) is unchanged.
    check(pc_show_outrun_miles_4413e2(true,false,0x474u),"4413E2 replaced");
    check(!pc_show_outrun_miles_4413e2(true,true,0x474u)&&!pc_show_outrun_miles_4413e2(true,false,0x476u),"4413CE unchanged");
    check(!pc_show_outrun_miles_4413e2(false,false,0x474u),"without the setting: the original");
    // Tweaks' "%s - %s %d" with (int) of the float.
    check(pc_outrun_miles_status("Not Signed In","OutRun Miles:",1234.9f)=="Not Signed In - OutRun Miles: 1234","the line, truncated");
    check(pc_outrun_miles_status("Not Signed In","OutRun Miles:",-0.75f)=="Not Signed In - OutRun Miles: 0","toward zero");
    check(pc_outrun_miles_status("a","b",16777216.0f)=="a - b 16777216","a large float");
    check(pc_outrun_miles_status("a","b",3.0e9f)=="a - b -2147483648","out of range: the integer indefinite");
    check(pc_outrun_miles_status("a","b",std::numeric_limits<float>::quiet_NaN())=="a - b -2147483648","NaN: the integer indefinite");
    check(pc_outrun_miles_status("Signed In: %s","OutRun Miles:",1.0f)=="Signed In: %s - OutRun Miles: 1","the status is not reformatted");
    std::printf("signin text: %d checks passed\n",checks);
    return 0;
}
