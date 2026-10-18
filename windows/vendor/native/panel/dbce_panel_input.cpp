// See dbce_panel_input.h.
#include "dbce_panel_input.h"
#include "dbce_panel.h"
#include "../third_party/imgui/imgui.h"
#include <windowsx.h>

namespace dbce { namespace panel {

namespace {
enum EvType { EV_KEY, EV_CHAR, EV_MOVE, EV_BUTTON, EV_WHEEL, EV_RESET };
struct Ev { EvType type; int a, b; };
const int kQueue = 512;
Ev g_q[kQueue];
int g_head, g_count;
CRITICAL_SECTION g_cs;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
HWND g_hwnd;
HHOOK g_hook;
DWORD g_hookThread;
bool g_wasOpen;

BOOL CALLBACK initOnce(PINIT_ONCE, PVOID, PVOID *) { InitializeCriticalSection(&g_cs); return TRUE; }

void push(EvType t, int a, int b)
{
    EnterCriticalSection(&g_cs);
    if (g_count < kQueue) { g_q[(g_head + g_count) % kQueue] = {t, a, b}; ++g_count; }
    LeaveCriticalSection(&g_cs);
}

bool ownsWindow(HWND w) { return g_hwnd && (w == g_hwnd || IsChild(g_hwnd, w)); }

POINT toClient(HWND from, LPARAM lp)
{
    POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (from != g_hwnd) MapWindowPoints(from, g_hwnd, &p, 1);
    return p;
}

// Returns true when the message is consumed (the game sees WM_NULL).
bool handle(MSG *m)
{
    const UINT msg = m->message;
    const bool open = IsOpen();
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        const int vk = (int)m->wParam;
        const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        const bool repeat = (m->lParam & (1 << 30)) != 0;
        const bool alt = (m->lParam & (1 << 29)) != 0;
        if (vk == VK_F6) { if (down && !repeat) Toggle(); return true; }
        if (vk == VK_F8 && StopFfbAvailable()) { if (down && !repeat) StopFfbFromKey(); return true; }
        if (!open) return false;
        if (alt && (vk == VK_TAB || vk == VK_F4)) return false;     // leave window switching and closing alone
        if (vk == VK_LWIN || vk == VK_RWIN) return false;
        if (vk == VK_ESCAPE) { if (down && !repeat) SetOpen(false); return true; }
        push(EV_KEY, vk, down ? 1 : 0);
        return true;
    }
    if (!open) return false;
    switch (msg) {
    case WM_CHAR: case WM_SYSCHAR:
        push(EV_CHAR, (int)m->wParam, 0); return true;
    case WM_MOUSEMOVE: {
        POINT p = toClient(m->hwnd, m->lParam); push(EV_MOVE, p.x, p.y); return true; }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: push(EV_BUTTON, 0, 1); return true;
    case WM_LBUTTONUP: push(EV_BUTTON, 0, 0); return true;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: push(EV_BUTTON, 1, 1); return true;
    case WM_RBUTTONUP: push(EV_BUTTON, 1, 0); return true;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: push(EV_BUTTON, 2, 1); return true;
    case WM_MBUTTONUP: push(EV_BUTTON, 2, 0); return true;
    case WM_MOUSEWHEEL: push(EV_WHEEL, GET_WHEEL_DELTA_WPARAM(m->wParam), 0); return true;
    case WM_MOUSEHWHEEL: return true;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: return true;
    case WM_INPUT: return true;   // the game's raw keyboard/mouse while the panel owns input
    default: return false;
    }
}

LRESULT CALLBACK getMessageHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE) {
        MSG *m = (MSG *)lp;
        if (m && ownsWindow(m->hwnd) && handle(m)) m->message = WM_NULL;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

ImGuiKey keyOf(int vk)
{
    switch (vk) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_SHIFT: return ImGuiKey_LeftShift;
    case VK_CONTROL: return ImGuiKey_LeftCtrl;
    case VK_MENU: return ImGuiKey_LeftAlt;
    default:
        if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
        if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
        return ImGuiKey_None;
    }
}
} // namespace

bool AttachInput(HWND w)
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    if (!w) return false;
    DWORD thread = GetWindowThreadProcessId(w, nullptr);
    EnterCriticalSection(&g_cs);
    bool ok = true;
    if (w != g_hwnd || thread != g_hookThread) {
        if (g_hook && thread != g_hookThread) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
        if (!g_hook) {
            // Thread-local hook in this process: no DLL injection, no window-procedure replacement.
            g_hook = SetWindowsHookExW(WH_GETMESSAGE, getMessageHook, nullptr, thread);
            ok = g_hook != nullptr;
            g_hookThread = ok ? thread : 0;
        }
        g_hwnd = w;
    }
    LeaveCriticalSection(&g_cs);
    return ok;
}

HWND InputWindow() { return g_hwnd; }

void DrainInput(int bw, int bh)
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    ImGuiIO &io = ImGui::GetIO();
    const bool open = IsOpen();
    // Releasing a cursor clip changes desktop state: only the panel's foreground
    // root may do it. Hidden/inactive hosts and missing window roots leave it alone.
    const HWND foreground = open && g_hwnd ? GetForegroundWindow() : nullptr;
    const HWND panelRoot = foreground ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
    const HWND foregroundRoot = foreground ? GetAncestor(foreground, GA_ROOT) : nullptr;
    const bool ownsForeground = panelRoot && foregroundRoot && panelRoot == foregroundRoot;
    if (open && !g_wasOpen) {
        // Fresh state on open: no stale keys, the pointer where the system cursor is, the cursor free to move.
        io.ClearInputKeys();
        POINT p;
        if (GetCursorPos(&p) && g_hwnd && ScreenToClient(g_hwnd, &p)) push(EV_MOVE, p.x, p.y);
        if (ownsForeground) ClipCursor(nullptr);
    }
    if (!open && g_wasOpen) io.ClearInputKeys();
    g_wasOpen = open;
    RECT rc = {0, 0, bw, bh};
    if (g_hwnd) GetClientRect(g_hwnd, &rc);
    const float sx = rc.right > 0 ? (float)bw / (float)rc.right : 1.0f;
    const float sy = rc.bottom > 0 ? (float)bh / (float)rc.bottom : 1.0f;
    Ev local[kQueue];
    int n = 0;
    EnterCriticalSection(&g_cs);
    for (; n < g_count; ++n) local[n] = g_q[(g_head + n) % kQueue];
    g_head = 0; g_count = 0;
    LeaveCriticalSection(&g_cs);
    for (int i = 0; i < n; ++i) {
        const Ev &e = local[i];
        switch (e.type) {
        case EV_KEY: {
            ImGuiKey k = keyOf(e.a);
            if (e.a == VK_SHIFT) io.AddKeyEvent(ImGuiMod_Shift, e.b != 0);
            if (e.a == VK_CONTROL) io.AddKeyEvent(ImGuiMod_Ctrl, e.b != 0);
            if (e.a == VK_MENU) io.AddKeyEvent(ImGuiMod_Alt, e.b != 0);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, e.b != 0);
            break; }
        case EV_CHAR: if (e.a > 0 && e.a < 0x10000) io.AddInputCharacterUTF16((unsigned short)e.a); break;
        case EV_MOVE: io.AddMousePosEvent(e.a * sx, e.b * sy); break;
        case EV_BUTTON: io.AddMouseButtonEvent(e.a, e.b != 0); break;
        case EV_WHEEL: io.AddMouseWheelEvent(0.0f, (float)e.a / (float)WHEEL_DELTA); break;
        default: break;
        }
    }
    if (open && ownsForeground) ClipCursor(nullptr);   // games re-clip the cursor every frame; the panel needs the whole window
}

}} // namespace dbce::panel
