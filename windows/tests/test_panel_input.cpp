// The F6 panel's input hook (vendor/native/panel/dbce_panel_input, WH_GETMESSAGE) on a real window of this thread,
// with posted keys as the muted harness and the dev channel's runs post them (zero 30367 point 4):
//   closed: every key but F6 reaches the game untouched (Esc, letters, F8 while no Stop FFB is offered);
//   F6 opens; open: keys go to the panel, Esc closes, Alt+Tab still passes;
//   Stop FFB offered: F8 stops it, open or closed.
#include "dbce_panel.h"
#include "dbce_panel_input.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"panel input: FAIL: %s\n",what);std::exit(1);}}
HWND window;
int stops=0;
bool ffb_on(){return true;}
void stop_ffb(){++stops;}
// Posts a key message and takes it as the game's pump would (PM_REMOVE runs the hook); the message the game then sees.
UINT deliver(UINT message,WPARAM vk,bool alt=false){
    LPARAM l=1|(LPARAM(MapVirtualKeyW(UINT(vk),MAPVK_VK_TO_VSC))<<16);
    if(alt)l|=LPARAM(1)<<29;
    if(message==WM_KEYUP||message==WM_SYSKEYUP)l|=(LPARAM(1)<<30)|(LPARAM(1)<<31);
    PostMessageW(window,message,vk,l);
    MSG m{};
    for(int i=0;i<100;++i){
        if(PeekMessageW(&m,window,0,0,PM_REMOVE)){
            if(m.message==WM_NULL||m.message==message)return m.message;
            continue;   // anything else the system posts to a new window
        }
        Sleep(1);
    }
    return 0xffffffffu;
}
bool passes(WPARAM vk,bool alt=false){
    const UINT down=alt?WM_SYSKEYDOWN:WM_KEYDOWN,up=alt?WM_SYSKEYUP:WM_KEYUP;
    return deliver(down,vk,alt)==down&&deliver(up,vk,alt)==up;
}
bool taken(WPARAM vk){return deliver(WM_KEYDOWN,vk)==WM_NULL&&deliver(WM_KEYUP,vk)==WM_NULL;}
}
int main(){
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"or2_panel_input_test";
    RegisterClassW(&wc);
    window=CreateWindowExW(0,wc.lpszClassName,L"panel input test",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,wc.hInstance,nullptr);
    check(window!=nullptr,"a test window");
    dbce::panel::Config c;c.modName="panel input test";c.iniPath=nullptr;   // a test process: nothing saved
    dbce::panel::Configure(c);
    check(dbce::panel::AttachInput(window),"the hook attaches to this thread's window");

    // Closed: everything but F6 reaches the game.
    check(!dbce::panel::IsOpen(),"closed at start");
    check(passes(VK_ESCAPE),"closed: Esc reaches the game");
    check(passes('A')&&passes(VK_UP)&&passes(VK_RETURN),"closed: letters, arrows and Enter reach the game");
    check(passes(VK_F8),"closed, no Stop FFB offered: F8 reaches the game");
    check(passes(VK_TAB,true),"closed: Alt+Tab reaches the game");
    check(!dbce::panel::IsOpen(),"still closed");
    // F6 opens; open: keys go to the panel; Alt+Tab still passes; Esc closes.
    check(taken(VK_F6)&&dbce::panel::IsOpen(),"F6 is taken and opens the panel");
    check(taken('A')&&taken(VK_UP),"open: keys go to the panel, not the game");
    check(passes(VK_TAB,true),"open: Alt+Tab still reaches the game");
    check(deliver(WM_KEYDOWN,VK_ESCAPE)==WM_NULL&&!dbce::panel::IsOpen(),"open: Esc is taken and closes");
    check(deliver(WM_KEYUP,VK_ESCAPE)==WM_KEYUP,"closed again: the Esc key-up reaches the game");
    check(passes('A')&&passes(VK_ESCAPE),"closed again: keys reach the game");
    // Stop FFB offered (STD-004): F8 stops it, open or closed, and is not passed on.
    c.ffbOn=ffb_on;c.stopFfb=stop_ffb;
    dbce::panel::Configure(c);
    check(dbce::panel::StopFfbAvailable(),"Stop FFB offered");
    check(taken(VK_F8)&&stops==1&&!dbce::panel::IsOpen(),"closed: F8 stops FFB and is taken");
    check(passes('A')&&passes(VK_ESCAPE),"closed with Stop FFB offered: other keys still reach the game");
    check(taken(VK_F6)&&dbce::panel::IsOpen(),"F6 opens");
    check(taken(VK_F8)&&stops==2,"open: F8 stops FFB");
    check(taken(VK_F6)&&!dbce::panel::IsOpen(),"F6 closes");
    DestroyWindow(window);
    std::printf("panel input: %d checks passed\n",checks);
    return 0;
}
