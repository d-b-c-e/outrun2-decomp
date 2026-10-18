// Shared F6 settings panel for native mods (STD-004, STD-017, STD-022, STD-030; UX-1 in docs/CONSUMER-UX.md).
//
// The panel owns the frame (title, View: Simple/Advanced, Close, Stop FFB, flat page tabs in the standard order) and
// a set of standard widgets; the mod draws each page's body with those widgets and keeps its settings in its own
// canonical config (the ini Wheelkit also reads). Drawing uses Dear ImGui (native/third_party/imgui); a renderer
// overlay (dbce_overlay_d3d11.h) calls Frame() once per presented frame. Input arrives through dbce_panel_input.h.
#pragma once
#include <windows.h>

namespace dbce { namespace panel {

// The five core pages in their fixed order (UX-01). A page the build does not offer is hidden (STD-017); Help stays.
enum Page { PAGE_CONTROLS, PAGE_FFB, PAGE_CAMERAS, PAGE_TELEMETRY, PAGE_HELP, PAGE_COUNT };

enum class Tone { Normal, Good, Warning, Error };

struct Config {
    const char *title = "Wheel settings";
    const char *modName = "";                   // shown in Help, e.g. "Rush Rally mod 0.3.0"
    const char *iniPath = nullptr;              // the mod's canonical config: [Panel] View, Scale and Page are kept there
    bool pages[PAGE_COUNT] = {false, false, false, false, true};
    void (*drawPage)(Page page, bool advanced) = nullptr;   // the mod draws each body with the widgets below
    // Force feedback the mod can stop (UX-05): the header's Stop FFB command and F8 call stopFfb, which turns the mod's
    // saved FFB setting Off; only the FFB page's Off/On turns it back on. ffbOn reports the current setting.
    bool (*ffbOn)() = nullptr;                  // null: no Stop FFB in this mod
    void (*stopFfb)() = nullptr;
    void (*log)(const char *line) = nullptr;
    // Unattended checks only (no OS input): a file whose text is "open <Page> [simple|advanced]" or "close" is applied
    // when it changes (polled twice a second by the overlay); a file last written before the process started is ignored.
    // Null: off.
    const char *devFile = nullptr;
    // STD-023: whether the three views are on, for the frame-rate readout and its log line. Null: not reported.
    bool (*triplesOn)() = nullptr;
    // The display in the mod's own words for that readout (for example "Surround span, one wide view"); when set, and
    // not empty, it replaces the triples on/off suffix, so a planar wide view is never called triples. Called on the
    // present thread. Null: triplesOn as before.
    const char *(*displayText)() = nullptr;
    // Help repository links, drawn as small text hyperlinks. Descriptors must live
    // as long as the panel; opening a link requires an explicit click. Null/empty
    // leaves that link out. Issue reporting stays in the mod's own Help action.
    const char *modRepositoryUrl = nullptr;
    const char *wheelkitRepositoryUrl = nullptr;
};

// Called once by the mod before the overlay starts (not from DllMain).
void Configure(const Config &config);

// True while the panel is open: the mod suppresses driving input and camera shortcuts (UX-03).
bool IsOpen();
void SetOpen(bool open);
void Toggle();
// Internal, passive status only. Text is copied (up to 255 UTF-8 bytes); null/empty clears.
// Safe from a host worker thread. No recorder, hotkeys, persistence or output is enabled.
// Hosts use SessionStatusPresentation wording and own terminal-state expiry. Saved must mean sealed.
enum class StatusTone { Recording, Busy, Success, Problem, Playback };
void SetStatusLine(const char *utf8Text, StatusTone tone);
void ClearStatusLine();
bool StatusLineActive();
// F8 (dbce_panel_input): Stop FFB when the mod offers it.
bool StopFfbAvailable();
void StopFfbFromKey();

// Overlay side, once per presented frame while the panel is open (an ImGui context must be current):
//   if (Prepare(w, h)) { BuildFonts(); <invalidate the renderer's font texture> }
//   <renderer NewFrame>; DrainInput(w, h); ImGui::NewFrame(); Draw(w, h); ImGui::Render(); <render draw data>
// Prepare returns true when the UI scale changed and the font atlas must be rebuilt (also on the first frame).
bool Prepare(int width, int height);
// The dev channel (Config::devFile); cheap enough to call every frame.
void PollDev();
// Every presented frame of the game's swap chain, open or closed (STD-023): ten-second frame-rate windows, shown on the
// Cameras page (Help when there is none) and logged every 30 s.
void NoteFrame();
void BuildFonts();
void Draw(int width, int height);
float Scale();

// ---- standard widgets (UX-02/03) ------------------------------------------------------------------------------
// Label column on the left, value/control on the right, help underneath in secondary text.
bool OffOn(const char *label, bool *value, const char *help = nullptr);
bool Choice(const char *label, const char *const *options, int count, int *value, const char *help = nullptr);
bool Percent(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *help = nullptr);
// A slider with its own unit, e.g. "%d\u00b0" for degrees (Percent is this with "%d%%").
bool Slider(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *format, const char *help = nullptr);
void Status(const char *label, const char *value, Tone tone = Tone::Normal);
void Heading(const char *text);
void Text(const char *text);                    // short wrapped help in secondary text
bool Command(const char *label);                // compact command, right-aligned on its own row
void BeginGroup(const char *id);                // a visibly bounded group (UX-03-G)
void EndGroup();
// A short "Saved" or error note beside the header controls (UX-08).
void NoteSaved(bool ok, const char *detail = nullptr);

// Panel preferences (view, scale, page) kept in [Panel] of the mod's ini.
bool Advanced();

}} // namespace dbce::panel
