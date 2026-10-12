/* wheelffb.h - the C ABI of WheelFfb.dll (dbce-wheel-mod-toolkit), with a
 * runtime loader for native mods.
 *
 * A dinput8 proxy or an injected DLL cannot afford a static import of
 * WheelFfb.dll: if the file is missing the loader would refuse to load the
 * proxy and the game would not start. Load it at runtime instead, beside your
 * own module, and disable force feedback when it is absent:
 *
 *     static WheelFfbApi ffb;
 *     if (WheelFfb_LoadBeside(&ffb, thisModule, L"WheelFfb.dll")) {
 *         ffb.SetLogPath("C:\\...\\ffb.log");        // optional; default %LOCALAPPDATA%\DbceWheel\ffb.log
 *         ffb.SetPreferredDeviceIndex(iniIndex);      // or SetPreferredDevice("MOZA")
 *         if (ffb.InitDirectInput((int)(INT_PTR)hwnd)) {
 *             ffb.InstallExitGuards();
 *             int road = ffb.CreatePeriodicEffect(25); // -1: synthesise below 15 Hz instead
 *         }
 *     } else log("WheelFfb.dll: %s missing", WheelFfb_MissingExport(&ffb));
 *
 * Every export is listed once, in WHEELFFB_API_LIST; typedefs, the struct and
 * the resolver are generated from it. The list must match WheelFfb.def - the
 * toolkit build checks that. The ABI only ever gains exports; check
 * GetWheelFfbVersion() (100 = 0.1.0, 200 = 0.2.0) before using a newer one,
 * or accept WheelFfb_Load's partial result via WheelFfb_MissingExport.
 *
 * All functions are __cdecl (the x86 .def exports them undecorated). Force
 * magnitudes are DirectInput's -10000..10000; frequencies for the periodic
 * effects are millihertz (25 Hz = 25000); condition coefficients, saturations,
 * deadbands and offsets are also -10000..10000.
 *
 * Native 0.6.0 finite bursts: CreatePeriodicBurst(hz, durationMs 1..500)
 * Shaped play adds phase 0..35999 and fadeMs 1..slot duration, strict API
 * readback before Start, and no physical-output guarantee. Legacy play clears
 * prior phase/envelope. CreatePeriodicBurst
 * returns an idle slot; PlayPeriodicBurst(slot, magnitude 0..10000, milliHz)
 * explicitly restarts one finite iteration (0 stops). StopPeriodicBurst is
 * idempotent for a valid burst slot. Frequency 1..100 Hz. Shares four periodic
 * slots; continuous Update rejects burst slots. Failed events are never retried.
 *
 * Native 0.9.0 ReadDeviceStateWithPov reads one coherent device snapshot.
 * axes must hold exactly 8 ints (X Y Z Rx Ry Rz Slider1 Slider2); POV exactly
 * 4 ints; buttonCount is 0..128 (NULL buttons allowed only at 0). Count fields
 * must equal those extents; pointers must address that much writable storage.
 * Buffers must not overlap. Buttons are 0/1. POV is clockwise hundredths of
 * degrees (0 north, 9000 east), -1 centered/absent/invalid. Driver low-word
 * FFFF and values above 35999 normalize to -1 without failing the snapshot.
 * Returns 1 only on successful state read. On failure, independently valid
 * buffers are cleared: axes/buttons to 0, POV to -1; invalid count buffers
 * are not touched. Zero axes are NOT a neutral pedal value: gate ALL output
 * on success. Optional for older DLLs; never split axes/POV into two reads.
 * Like the existing input API, serialize reads with open/close/FFB lifecycle
 * on the consumer's device owner thread. Open FFB before its shared read slot;
 * close/reopen readers when promoting an input-only device to FFB ownership.
 *
 * Test injection (STD-033 section 6), development only. EnableTestInjection(1) latches an irreversible no-force
 * mode for the process, and only before any force path was ever used (InitDirectInput, Aquire, an effect, an
 * external session; attempted counts). Then every force export refuses before touching a device, an effect or a
 * hook, reads keep working through OpenReadDevice's own non-exclusive handles, and SubmitTestInjection adds a raw
 * sample ("inject raw <axis|button|hat> <index> [angle] dev=<instance> value=<raw> ms=<50-15000>", printable
 * ASCII, 1-512 bytes) that ReadDeviceState and ReadDeviceStateWithPov deliver into the next successful reads of the
 * read slot with that instance GUID. LastReadInjected(slot) reports whether the slot's last read attempt actually
 * carried one; a failed read, CloseReadDevices and FreeDirectInput drop running samples. ReadAuxButtons never
 * injects. GetTestInjectionStatus: "off", "refused: ..." or "armed: no force; ...".
 *
 * Three families of effect, and they are not interchangeable:
 *   constant   SetDeviceForcesXY - the force your model computes
 *   periodic   CreatePeriodicEffect - a waveform the base renders itself
 *   condition  CreateConditionEffect - spring, damper, inertia, friction, where
 *              the base computes force from axis position or velocity
 *              continuously. A hardware damper is smoother and more stable than
 *              damping synthesised at 60 Hz, and is usually the right answer to
 *              a direct-drive base that oscillates. Never call FreeLibrary on the module
 * from DllMain - unload from your own shutdown path, after PanicStop.
 */
#ifndef DBCE_WHEELFFB_H
#define DBCE_WHEELFFB_H

#include <windows.h>
#include <string.h>
#include "external_ffb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* X(return type, name, parameter list) */
#define WHEELFFB_API_LIST(X) \
    X(int,  InitDirectInput,        (int hwnd)) \
    X(void, Aquire,                 (void)) \
    X(int,  SetDeviceForcesXY,      (int x, int y)) \
    X(int,  StartEffect,            (void)) \
    X(int,  StopEffect,             (void)) \
    X(int,  SetAutoCenter,          (int enable)) \
    X(void, FreeDirectInput,        (void)) \
    X(void, SetPreferredDevice,     (const char* name)) \
    X(void, SetPreferredDeviceGuid, (const void* guid16)) \
    X(void, SetPreferredDeviceIndex,(int index)) \
    X(int,  EnumerateDevices,       (void)) \
    X(int,  GetDeviceName,          (int index, char* buffer, int size)) \
    X(int,  EnumerateAllDevices,    (void)) \
    X(int,  GetAnyDeviceName,       (int index, char* buffer, int size)) \
    X(int,  OpenAuxDevice,          (int index)) \
    X(int,  ReadAuxButtons,         (unsigned char* buffer, int length)) \
    X(void, CloseAuxDevice,         (void)) \
    X(int,  OpenReadDevice,         (int index)) \
    X(int,  ReadDeviceState,        (int slot, int* axes, unsigned char* buttons, int buttonCount)) \
    X(void, CloseReadDevices,       (void)) \
    X(int,  GetDeviceInfo,          (int index, int* axes, int* buttons)) \
    X(int,  GetDeviceGuid,          (int index, void* guid16)) \
    X(int,  GetAnyDeviceGuid,       (int index, void* guid16)) \
    X(int,  GetAnyDeviceInfo,       (int index, int* axes, int* buttons, int* ffb)) \
    X(void, PanicStop,              (void)) \
    X(void, ZeroForces,             (void)) \
    X(int,  InstallExitGuards,      (void)) \
    X(void, SetHoldTimeoutMs,       (int ms)) \
    X(void, SetLogPath,             (const char* path)) \
    X(int,  GetLastHResult,         (void)) \
    X(int,  GetWheelFfbVersion,     (void)) \
    X(int,  CreatePeriodicEffect,   (int freqHz)) \
    X(int,  UpdatePeriodicEffect,   (int slot, int magnitude, int freqMilliHz)) \
    X(int,  CreatePeriodicBurst,    (int freqHz, int durationMs)) \
    X(int,  PlayPeriodicBurst,      (int slot, int magnitude, int freqMilliHz)) \
    X(int,  PlayShapedPeriodicBurst,      (int slot, int magnitude, int freqMilliHz, int phaseHundredths, int fadeMs)) \
    X(int,  CreateConstantBurst, (int durationMs)) \
    X(int,  PlayConstantBurst, (int slot, int signedMagnitude)) \
    X(int,  StopConstantBurst, (int slot)) \
    X(void, ReleaseConstantBursts, (void)) \
    X(int,  StopPeriodicBurst,      (int slot)) \
    X(void, ReleasePeriodicEffects, (void)) \
    X(int,  CreateConditionEffect,  (int type)) \
    X(int,  UpdateConditionEffect,  (int slot, int coefficient, int saturation, int deadband, int offset)) \
    X(void, ReleaseConditionEffects,(void)) \
    X(void, SetStrictDeviceSelection,(int enabled)) \
    X(int, ReadDeviceStateWithPov, (int slot, int* axes, int axisCount, unsigned char* buttons, int buttonCount, int* pov, int povCount)) \
    X(int, ExternalFfbEnter, (HWND ownerWindow)) \
    X(int, ExternalFfbOpen, (const ExternalFfbConfigV1* config, const wchar_t* targetImagePath, unsigned int pathChars, unsigned long long* session)) \
    X(int, ExternalFfbArm, (unsigned long long session, ExternalFfbArmTicketV1* ticket)) \
    X(int, ExternalFfbRenewClient, (unsigned long long session, const ExternalFfbClientFrameV1* frame)) \
    X(int, ExternalFfbSubmitConstant, (unsigned long long session, const ExternalFfbForceFrameV1* frame)) \
    X(int, ExternalFfbDisarm, (unsigned long long session, unsigned int reason)) \
    X(int, ExternalFfbClose, (unsigned long long session)) \
    X(int, ExternalFfbGetStatus, (unsigned long long session, ExternalFfbStatusV1* status)) \
    X(int, EnableTestInjection, (int flags)) \
    X(int, SubmitTestInjection, (const char* line, char* why, int whySize)) \
    X(int, GetTestInjectionStatus, (char* buf, int size)) \
    X(int, LastReadInjected, (int slot))

#define WHEELFFB_TYPEDEF(ret, name, args) typedef ret (__cdecl *WheelFfb_##name##_t) args;
WHEELFFB_API_LIST(WHEELFFB_TYPEDEF)
#undef WHEELFFB_TYPEDEF

typedef struct WheelFfbApi {
    HMODULE module;
#define WHEELFFB_MEMBER(ret, name, args) WheelFfb_##name##_t name;
    WHEELFFB_API_LIST(WHEELFFB_MEMBER)
#undef WHEELFFB_MEMBER
} WheelFfbApi;

/* Loads WheelFfb.dll from `path` (a full path is strongly preferred: a bare
 * name searches the process's DLL path, which for a game means its exe folder
 * and then the system). Returns 1 when the module loaded and every export
 * resolved; 0 otherwise, with whatever did resolve left usable. */
static int WheelFfb_Load(WheelFfbApi* api, const wchar_t* path)
{
    int ok = 1;
    memset(api, 0, sizeof(*api));
    api->module = LoadLibraryW(path ? path : L"WheelFfb.dll");
    if (!api->module) return 0;
#define WHEELFFB_RESOLVE(ret, name, args) \
    api->name = (WheelFfb_##name##_t)GetProcAddress(api->module, #name); if (!api->name) ok = 0;
    WHEELFFB_API_LIST(WHEELFFB_RESOLVE)
#undef WHEELFFB_RESOLVE
    return ok;
}

/* Loads `fileName` from the folder that contains `self` (your own DLL's
 * HMODULE from DllMain) - the game's working directory is not reliable. */
static int WheelFfb_LoadBeside(WheelFfbApi* api, HMODULE self, const wchar_t* fileName)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash;
    if (n == 0 || n >= MAX_PATH) { memset(api, 0, sizeof(*api)); return 0; }
    slash = wcsrchr(path, L'\\');
    if (!slash) { memset(api, 0, sizeof(*api)); return 0; }
    slash[1] = 0;
    if (wcslen(path) + wcslen(fileName ? fileName : L"WheelFfb.dll") >= MAX_PATH) { memset(api, 0, sizeof(*api)); return 0; }
    wcscat_s(path, MAX_PATH, fileName ? fileName : L"WheelFfb.dll");
    return WheelFfb_Load(api, path);
}

/* Name of the first export that did not resolve, "module" if the DLL itself
 * did not load, or NULL when everything is present. For the log line. */
static const char* WheelFfb_MissingExport(const WheelFfbApi* api)
{
    if (!api->module) return "module";
#define WHEELFFB_CHECK(ret, name, args) if (!api->name) return #name;
    WHEELFFB_API_LIST(WHEELFFB_CHECK)
#undef WHEELFFB_CHECK
    return NULL;
}

/* Frees the module. Call from your own shutdown path after PanicStop, never
 * from DllMain(DLL_PROCESS_DETACH). */
static void WheelFfb_Unload(WheelFfbApi* api)
{
    if (api->module) FreeLibrary(api->module);
    memset(api, 0, sizeof(*api));
}

#ifdef __cplusplus
}
#endif
#endif /* DBCE_WHEELFFB_H */
