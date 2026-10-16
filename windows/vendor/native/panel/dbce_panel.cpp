// Shared F6 settings panel: frame, navigation and standard widgets (see dbce_panel.h; UX-1 in docs/CONSUMER-UX.md).
#include "dbce_panel.h"
#include "dbce_frame_stats.hpp"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace dbce { namespace panel {

static Config g_cfg;
static volatile LONG g_open;
static Page g_page = PAGE_HELP;
static bool g_pageChanged = true;
static bool g_advanced;
static int g_userScale = 100;          // percent, Help > UI scale
static float g_scale = 1.0f;            // effective, from the window height and the user scale
static float g_fontScale = 0.0f;        // scale the font atlas was built for
static ImFont *g_body, *g_small, *g_heading;
static char g_note[160];
static bool g_noteOk = true;
static ULONGLONG g_noteAt;
static FrameWindow g_frames;            // STD-023, present thread only
static LARGE_INTEGER g_frameFreq, g_frameLast;
static ULONGLONG g_frameLoggedAt;

static const char *const kPageNames[PAGE_COUNT] = {"Controls", "FFB", "Cameras", "Telemetry", "Help"};

// ---- colours (UX-03: opaque dark neutral panel, one accent for selection) ------------------------------------
static const ImVec4 kBg(0.118f, 0.129f, 0.149f, 1.0f);        // #1E2126
static const ImVec4 kGroup(0.149f, 0.165f, 0.188f, 1.0f);     // #262A30
static const ImVec4 kBorder(0.271f, 0.294f, 0.333f, 1.0f);    // #454B55
static const ImVec4 kText(0.910f, 0.918f, 0.929f, 1.0f);      // #E8EAED
static const ImVec4 kSecondary(0.659f, 0.682f, 0.718f, 1.0f); // #A8AEB7
static const ImVec4 kButton(0.212f, 0.231f, 0.263f, 1.0f);    // #363B43
static const ImVec4 kButtonHover(0.255f, 0.278f, 0.318f, 1.0f);
static const ImVec4 kAccent(0.239f, 0.545f, 0.992f, 1.0f);    // #3D8BFD
static const ImVec4 kAccentHover(0.353f, 0.620f, 1.0f, 1.0f);
static const ImVec4 kGood(0.298f, 0.686f, 0.478f, 1.0f);
static const ImVec4 kWarning(0.878f, 0.627f, 0.188f, 1.0f);
static const ImVec4 kError(0.878f, 0.314f, 0.314f, 1.0f);

static void logf(const char *fmt, ...)
{
    if (!g_cfg.log) return;
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(b, sizeof b, _TRUNCATE, fmt, ap);
    va_end(ap);
    g_cfg.log(b);
}

// ---- preferences in [Panel] of the mod's ini ------------------------------------------------------------------
static void loadPrefs()
{
    if (!g_cfg.iniPath) return;
    char v[32] = "";
    GetPrivateProfileStringA("Panel", "View", "Simple", v, sizeof v, g_cfg.iniPath);
    g_advanced = _stricmp(v, "Advanced") == 0;
    g_userScale = (int)GetPrivateProfileIntA("Panel", "Scale", 100, g_cfg.iniPath);
    g_userScale = (std::min)(200, (std::max)(75, g_userScale));
    GetPrivateProfileStringA("Panel", "Page", "", v, sizeof v, g_cfg.iniPath);
    for (int p = 0; p < PAGE_COUNT; ++p)   // by name: a saved page keeps its meaning if pages are added (UX-01)
        if (!_stricmp(v, kPageNames[p]) && g_cfg.pages[p]) g_page = (Page)p;
}
static void savePref(const char *key, const char *value)
{
    if (!g_cfg.iniPath) return;
    if (!WritePrivateProfileStringA("Panel", key, value, g_cfg.iniPath)) {
        NoteSaved(false, "Settings could not be saved");
        logf("panel: could not save [Panel] %s (%lu)", key, GetLastError());
    }
}

void Configure(const Config &config)
{
    g_cfg = config;
    g_cfg.pages[PAGE_HELP] = true;   // Help always stays (STD-017)
    g_page = PAGE_HELP;
    for (int p = 0; p < PAGE_COUNT; ++p) if (g_cfg.pages[p]) { g_page = (Page)p; break; }   // first offered page
    loadPrefs();
}

bool IsOpen() { return g_open != 0; }
void SetOpen(bool open)
{
    InterlockedExchange(&g_open, open ? 1 : 0);
    if (open) g_pageChanged = true;
}
void Toggle() { SetOpen(!IsOpen()); }
bool StopFfbAvailable() { return g_cfg.ffbOn && g_cfg.stopFfb; }
void StopFfbFromKey()
{
    if (!StopFfbAvailable() || !g_cfg.ffbOn()) return;
    g_cfg.stopFfb();
    NoteSaved(true, "FFB stopped");
    logf("panel: F8 stopped force feedback");
}
bool Advanced() { return g_advanced; }
float Scale() { return g_scale; }

void NoteSaved(bool ok, const char *detail)
{
    g_noteOk = ok;
    strcpy_s(g_note, ok ? (detail ? detail : "Saved") : (detail ? detail : "Not saved"));
    g_noteAt = GetTickCount64();
}

// ---- scale and fonts -----------------------------------------------------------------------------------------
// Logical units: 16 body, 14 secondary, 20 headings; 8-unit grid, 16 padding, 32 minimum row height (UX-03), all scaled
// together. The base follows the window height (1080 = 1.0, never below 1.0 so 720p stays readable) times UI scale.
bool Prepare(int width, int height)
{
    (void)width;
    float s = (std::max)(1.0f, (float)height / 1080.0f) * (float)g_userScale / 100.0f;
    s = (std::min)(4.0f, (std::max)(0.75f, s));
    g_scale = s;
    return std::fabs(g_fontScale - s) > 0.01f;
}

void BuildFonts()
{
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    const float s = g_scale;
    ImFontConfig fc;
    fc.OversampleH = 2;
    char win[MAX_PATH], body[MAX_PATH], bold[MAX_PATH];
    GetWindowsDirectoryA(win, MAX_PATH);
    sprintf_s(body, "%s\\Fonts\\segoeui.ttf", win);
    sprintf_s(bold, "%s\\Fonts\\seguisb.ttf", win);
    bool haveBody = GetFileAttributesA(body) != INVALID_FILE_ATTRIBUTES;
    bool haveBold = GetFileAttributesA(bold) != INVALID_FILE_ATTRIBUTES;
    if (haveBody) {
        g_body = io.Fonts->AddFontFromFileTTF(body, 16.0f * s, &fc);
        g_small = io.Fonts->AddFontFromFileTTF(body, 14.0f * s, &fc);
        g_heading = io.Fonts->AddFontFromFileTTF(haveBold ? bold : body, 20.0f * s, &fc);
    }
    if (!g_body) {   // no Segoe UI: the built-in font, scaled
        ImFontConfig d;
        d.SizePixels = 16.0f * s;
        g_body = io.Fonts->AddFontDefault(&d);
        d.SizePixels = 14.0f * s; g_small = io.Fonts->AddFontDefault(&d);
        d.SizePixels = 20.0f * s; g_heading = io.Fonts->AddFontDefault(&d);
    }
    io.FontDefault = g_body;
    g_fontScale = s;

    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(16 * s, 16 * s);
    st.FramePadding = ImVec2(12 * s, 8 * s);          // 16 px text + 2 x 8 = 32 px rows
    st.ItemSpacing = ImVec2(8 * s, 8 * s);
    st.ItemInnerSpacing = ImVec2(8 * s, 8 * s);
    st.CellPadding = ImVec2(8 * s, 4 * s);
    st.ScrollbarSize = 16 * s;
    st.GrabMinSize = 16 * s;
    st.WindowRounding = 6 * s;
    st.ChildRounding = 4 * s;
    st.FrameRounding = 4 * s;
    st.GrabRounding = 4 * s;
    st.WindowBorderSize = 1;
    st.ChildBorderSize = 1;
    st.FrameBorderSize = 0;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kSecondary;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = kGroup;
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_FrameBg] = kButton;
    c[ImGuiCol_FrameBgHovered] = kButtonHover;
    c[ImGuiCol_FrameBgActive] = kButtonHover;
    c[ImGuiCol_Button] = kButton;
    c[ImGuiCol_ButtonHovered] = kButtonHover;
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_Header] = kButton;
    c[ImGuiCol_HeaderHovered] = kButtonHover;
    c[ImGuiCol_HeaderActive] = kAccent;
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_ScrollbarBg] = kBg;
    c[ImGuiCol_ScrollbarGrab] = kButton;
    c[ImGuiCol_ScrollbarGrabHovered] = kButtonHover;
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_NavHighlight] = kAccentHover;           // keyboard focus, distinct from the selected tab underline
}

// ---- layout helpers ------------------------------------------------------------------------------------------
static float labelWidth() { return (std::max)(200.0f * g_scale, ImGui::GetContentRegionAvail().x * 0.38f); }

static void secondaryWrapped(const char *text)
{
    ImGui::PushFont(g_small);
    ImGui::PushStyleColor(ImGuiCol_Text, kSecondary);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static float buttonWidth(const char *label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x;
}

// A segmented control: short mutually exclusive values, the selected one filled with the accent (UX-02/03-C).
static bool segments(const char *id, const char *const *options, int count, int *value)
{
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, 2 * g_scale);
        const bool on = *value == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? kAccent : kButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kAccentHover : kButtonHover);
        ImGui::PushID(i);
        if (ImGui::Button(options[i]) && !on) { *value = i; changed = true; }
        ImGui::PopID();
        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    return changed;
}

static void rowLabel(const char *label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth());
}

// ---- widgets ------------------------------------------------------------------------------------------------
bool OffOn(const char *label, bool *value, const char *help)
{
    static const char *const kOffOn[2] = {"Off", "On"};
    int v = *value ? 1 : 0;
    rowLabel(label);
    bool changed = segments(label, kOffOn, 2, &v);
    if (changed) *value = v == 1;
    if (help) secondaryWrapped(help);
    return changed;
}

bool Choice(const char *label, const char *const *options, int count, int *value, const char *help)
{
    rowLabel(label);
    bool changed;
    float need = 0;
    for (int i = 0; i < count; ++i) need += buttonWidth(options[i]) + 2 * g_scale;
    if (count <= 4 && need <= ImGui::GetContentRegionAvail().x) changed = segments(label, options, count, value);
    else {
        ImGui::SetNextItemWidth((std::min)(ImGui::GetContentRegionAvail().x, 360 * g_scale));
        changed = ImGui::Combo((std::string("##") + label).c_str(), value, options, count);
    }
    if (help) secondaryWrapped(help);
    return changed;
}

bool Percent(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *help)
{
    return Slider(label, value, minValue, maxValue, defaultValue, "%d%%", help);
}

bool Slider(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *format, const char *help)
{
    rowLabel(label);
    const char *reset = "Default";
    float avail = ImGui::GetContentRegionAvail().x - buttonWidth(reset) - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth((std::max)(120 * g_scale, avail));
    bool changed = ImGui::SliderInt((std::string("##") + label).c_str(), value, minValue, maxValue, format, ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::PushID(label);
    ImGui::BeginDisabled(*value == defaultValue);
    if (ImGui::Button(reset)) { *value = defaultValue; changed = true; }
    ImGui::EndDisabled();
    ImGui::PopID();
    if (help) secondaryWrapped(help);
    return changed;
}

void Status(const char *label, const char *value, Tone tone)
{
    rowLabel(label);
    const ImVec4 col = tone == Tone::Good ? kGood : tone == Tone::Warning ? kWarning : tone == Tone::Error ? kError : kText;
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void Heading(const char *text)
{
    ImGui::Dummy(ImVec2(0, 4 * g_scale));
    ImGui::PushFont(g_heading);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void Text(const char *text) { secondaryWrapped(text); }

bool Command(const char *label)
{
    float w = buttonWidth(label);
    float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    return ImGui::Button(label);
}

void BeginGroup(const char *id)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kGroup);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * g_scale, 12 * g_scale));
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
void EndGroup()
{
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 8 * g_scale));   // 16 between groups (8 here + item spacing) versus 8 within (UX-03-G)
}

// ---- frame rate (STD-023) -----------------------------------------------------------------------------------
static void frameLine(char *out, size_t size, const FrameSummary &s)
{
    const char *display = g_cfg.displayText ? g_cfg.displayText() : nullptr;
    const int triples = g_cfg.triplesOn ? (g_cfg.triplesOn() ? 1 : 0) : -1;
    FormatFrameLine(out, size, s, display, triples);
}
void NoteFrame()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_frameFreq.QuadPart) QueryPerformanceFrequency(&g_frameFreq);
    if (g_frameLast.QuadPart) {
        const double ms = (double)(now.QuadPart - g_frameLast.QuadPart) * 1000.0 / (double)g_frameFreq.QuadPart;
        if (g_frames.Add(ms)) {
            const ULONGLONG t = GetTickCount64();
            if (!g_frameLoggedAt || t - g_frameLoggedAt >= 30000) {
                char line[160];
                frameLine(line, sizeof line, g_frames.Last());
                logf("frame rate (10 s): %s", line);
                g_frameLoggedAt = t;
            }
        }
    }
    g_frameLast = now;
}
static void drawFrameRate()
{
    BeginGroup("frame-rate");
    const FrameSummary &s = g_frames.Last();
    char line[160] = "Measuring (10 s)";
    if (s.valid) frameLine(line, sizeof line, s);
    Status("Frame rate", line);
    if (Advanced()) Text("The last ten seconds, measured at the game's present; the same line goes to the log every 30 s.");
    EndGroup();
}

// ---- Help page (always present) -----------------------------------------------------------------------------
static void drawHelp(bool advanced)
{
    BeginGroup("help-version");
    Status("Version", g_cfg.modName && *g_cfg.modName ? g_cfg.modName : "unknown");
    Status("Keys", g_cfg.ffbOn ? "F6 opens and closes this panel. F8 stops force feedback. Esc closes." : "F6 opens and closes this panel. Esc closes.");
    EndGroup();
    BeginGroup("help-scale");
    int scale = g_userScale;
    if (Percent("UI scale", &scale, 75, 200, 100, "Text and controls grow or shrink together; the panel follows the window height.")) {
        g_userScale = scale;
        char v[16]; sprintf_s(v, "%d", scale);
        savePref("Scale", v);
    }
    EndGroup();
    const bool modLink = g_cfg.modRepositoryUrl && *g_cfg.modRepositoryUrl;
    const bool wheelkitLink = g_cfg.wheelkitRepositoryUrl && *g_cfg.wheelkitRepositoryUrl;
    if (modLink || wheelkitLink) {
        BeginGroup("help-repositories");
        ImGui::PushFont(g_small);
        if (modLink) ImGui::TextLinkOpenURL("Mod on GitHub", g_cfg.modRepositoryUrl);
        if (wheelkitLink) ImGui::TextLinkOpenURL("Wheelkit on GitHub", g_cfg.wheelkitRepositoryUrl);
        ImGui::PopFont();
        EndGroup();
    }
    if (g_cfg.drawPage) g_cfg.drawPage(PAGE_HELP, advanced);
    if (!g_cfg.pages[PAGE_CAMERAS]) drawFrameRate();
}

// ---- the frame ----------------------------------------------------------------------------------------------
void Draw(int width, int height)
{
    if (!IsOpen()) return;
    const float s = g_scale;
    // Placement: on the centre screen of a three-screen span (STD-019), centred in the window otherwise.
    float x0 = 0, x1 = (float)width;
    if (width >= height * 2.5f) { x0 = width / 3.0f; x1 = 2.0f * width / 3.0f; }
    float w = (std::min)(x1 - x0 - 48 * s, 960 * s), h = (std::min)((float)height - 96 * s, 820 * s);
    w = (std::max)(w, (std::min)(x1 - x0, 480 * s));
    h = (std::max)(h, (std::min)((float)height, 360 * s));
    ImGui::SetNextWindowPos(ImVec2(x0 + (x1 - x0 - w) / 2, (height - h) / 2), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##dbce-panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Header: title left; View, Stop FFB and Close right (UX-01, UX-05).
    ImGui::PushFont(g_heading);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(g_cfg.title);
    ImGui::PopFont();
    static const char *const kViews[2] = {"Simple", "Advanced"};
    const ImGuiStyle &st = ImGui::GetStyle();
    float right = buttonWidth("Close");
    const bool ffb = g_cfg.ffbOn && g_cfg.stopFfb;
    const bool ffbOn = ffb && g_cfg.ffbOn();
    const char *stopLabel = ffbOn ? "Stop FFB" : "FFB off";
    if (ffb) right += buttonWidth(stopLabel) + st.ItemSpacing.x;
    right += ImGui::CalcTextSize("View:").x + st.ItemSpacing.x + buttonWidth("Simple") + buttonWidth("Advanced") + 2 * s + st.ItemSpacing.x * 2;
    bool noteVisible = g_note[0] && GetTickCount64() - g_noteAt < 4000;
    float noteW = noteVisible ? ImGui::CalcTextSize(g_note).x + st.ItemSpacing.x * 2 : 0;
    ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + st.ItemSpacing.x, ImGui::GetWindowContentRegionMax().x - right - noteW));
    if (noteVisible) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, g_noteOk ? kGood : kError);
        ImGui::TextUnformatted(g_note);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, st.ItemSpacing.x * 2);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("View:");
    ImGui::SameLine();
    int view = g_advanced ? 1 : 0;
    if (segments("view", kViews, 2, &view)) { g_advanced = view == 1; savePref("View", g_advanced ? "Advanced" : "Simple"); }
    if (ffb) {
        ImGui::SameLine(0, st.ItemSpacing.x * 2);
        ImGui::BeginDisabled(!ffbOn);
        if (ImGui::Button(stopLabel)) g_cfg.stopFfb();
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) SetOpen(false);

    // Page tabs: flat labels with an accent underline for the selected page (UX-03).
    ImGui::Dummy(ImVec2(0, 4 * s));
    bool firstTab = true;
    for (int p = 0; p < PAGE_COUNT; ++p) {
        if (!g_cfg.pages[p]) continue;
        if (!firstTab) ImGui::SameLine(0, 24 * s);
        firstTab = false;
        const bool sel = g_page == p;
        ImVec2 sz = ImGui::CalcTextSize(kPageNames[p]);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(p);
        if (ImGui::InvisibleButton("tab", ImVec2(sz.x, sz.y + 10 * s))) {
            if (g_page != p) { g_page = (Page)p; g_pageChanged = true; savePref("Page", kPageNames[p]); }
        }
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddText(pos, ImGui::GetColorU32(sel || hov ? kText : kSecondary), kPageNames[p]);
        if (sel) dl->AddRectFilled(ImVec2(pos.x, pos.y + sz.y + 6 * s), ImVec2(pos.x + sz.x, pos.y + sz.y + 9 * s), ImGui::GetColorU32(kAccent));
    }
    ImGui::Separator();

    // Page body: scrollable, scroll reset when the page changes.
    ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (g_pageChanged) { ImGui::SetScrollY(0); g_pageChanged = false; }
    if (!g_cfg.pages[g_page]) g_page = PAGE_HELP;
    if (g_page == PAGE_HELP) drawHelp(g_advanced);
    else if (g_cfg.drawPage) g_cfg.drawPage(g_page, g_advanced);
    if (g_page == PAGE_CAMERAS) drawFrameRate();
    ImGui::EndChild();
    ImGui::End();
}


// ---- dev channel (unattended checks; Config::devFile) --------------------------------------------------------
void PollDev()
{
    static ULONGLONG nextAt;
    static FILETIME seen;
    if (!g_cfg.devFile) return;
    ULONGLONG now = GetTickCount64();
    if (now < nextAt) return;
    nextAt = now + 500;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(g_cfg.devFile, GetFileExInfoStandard, &a)) return;
    if (CompareFileTime(&a.ftLastWriteTime, &seen) == 0) return;
    seen = a.ftLastWriteTime;
    // A command left from an earlier run (written before this process started) is never applied: the owner's own
    // launch must not open the panel.
    static FILETIME started;
    if (!started.dwLowDateTime && !started.dwHighDateTime) {
        FILETIME exitT, kernelT, userT;
        if (!GetProcessTimes(GetCurrentProcess(), &started, &exitT, &kernelT, &userT)) started.dwLowDateTime = 1;
    }
    if (CompareFileTime(&a.ftLastWriteTime, &started) < 0) return;
    char text[128] = "";
    HANDLE f = CreateFileA(g_cfg.devFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    ReadFile(f, text, sizeof text - 1, &got, nullptr);
    CloseHandle(f);
    text[got] = 0;
    char verb[16] = "", page[16] = "", view[16] = "";
    sscanf_s(text, "%15s %15s %15s", verb, (unsigned)sizeof verb, page, (unsigned)sizeof page, view, (unsigned)sizeof view);
    if (!_stricmp(verb, "close")) { SetOpen(false); logf("panel: dev channel closed the panel"); return; }
    if (_stricmp(verb, "open")) return;
    for (int p = 0; p < PAGE_COUNT; ++p) if (!_stricmp(page, kPageNames[p]) && g_cfg.pages[p]) { g_page = (Page)p; g_pageChanged = true; }
    if (view[0]) g_advanced = !_stricmp(view, "advanced");   // not saved: a test does not change the owner's view
    SetOpen(true);
    logf("panel: dev channel opened %s (%s)", kPageNames[g_page], g_advanced ? "Advanced" : "Simple");
}
}} // namespace dbce::panel
