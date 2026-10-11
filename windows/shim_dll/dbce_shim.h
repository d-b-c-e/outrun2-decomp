#pragma once
/* dbce_shim: the shared input shim (windows/shim Core + DInputProvider, windows/host ProviderHost) behind an experimental
   C ABI for managed game adapters (Astra 16982/17050). ABI version 1: not a stable-ABI promise.
   - The handle belongs to the thread that opened it (the game's input thread). Every call taking a handle must be made on
     that thread; dbce_shim_abi_version, dbce_shim_frame_size and dbce_shim_action_name may be called from any thread.
   - No callbacks. Text is UTF-8 except the folder (UTF-16). Buffers are caller-owned and bounded: text is truncated,
     always NUL-terminated, and `needed` (when given) receives the full length plus one.
   - Errors are bounded result codes (below); nothing throws across the boundary.
   - Ownership follows the host's P21 lifecycle: dbce_shim_stop runs the bounded stop on the input thread. A resolved stop
     frees the host; an unresolved one leaves the host and its workers alive until process exit (never torn down under
     a running worker). Either way the handle is spent: later calls return DBCE_SHIM_STOPPED.
   - The shim reads devices non-exclusively and never creates force effects. Version 1 is ordinary (not latched): no
     control worker, no capture. Latched test sessions and the recorder are the next seams. */
#include <stdint.h>
#ifdef DBCE_SHIM_BUILD
#define DBCE_SHIM_API __declspec(dllexport)
#else
#define DBCE_SHIM_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define DBCE_SHIM_ABI 1
#define DBCE_SHIM_ACTIONS 31

enum {
    DBCE_SHIM_OK = 0,
    DBCE_SHIM_UNRESOLVED = 1,      /* dbce_shim_stop: the host did not resolve; it stays alive until process exit */
    DBCE_SHIM_BAD_ARGUMENT = -1,   /* null, wrong size, bad handle or a value out of range */
    DBCE_SHIM_WRONG_THREAD = -2,   /* not the thread that opened the handle */
    DBCE_SHIM_REFUSED = -3,        /* the host or its devices refused; see the why buffer */
    DBCE_SHIM_STOPPED = -4         /* the handle was stopped */
};

/* One action of a Core frame. status is dbce::shim::Status, value is normalized (steer -1..1, others 0..1). 8 bytes. */
typedef struct dbce_shim_action {
    float value;
    uint8_t status;
    uint8_t injected;
    uint8_t reserved[2];
} dbce_shim_action;

/* One Core frame: what a game adapter consumes. Every field is naturally aligned; 296 bytes (asserted). */
typedef struct dbce_shim_frame {
    uint32_t struct_size;          /* in: sizeof(dbce_shim_frame) as the caller allocated it; must equal dbce_shim_frame_size() */
    uint32_t abi;                  /* out: DBCE_SHIM_ABI */
    uint64_t seq;
    int64_t tick_us;
    uint64_t down, up;             /* digital edges against the previous frame, bit = action index */
    uint8_t run;                   /* dbce::shim::Run */
    uint8_t latched;
    uint8_t suppress_native;
    uint8_t reserved0;
    uint32_t sample_count;
    dbce_shim_action action[DBCE_SHIM_ACTIONS];
} dbce_shim_frame;

DBCE_SHIM_API int32_t dbce_shim_abi_version(void);
DBCE_SHIM_API int32_t dbce_shim_frame_size(void);
/* The standard action id at index 0..DBCE_SHIM_ACTIONS-1 (a static string), or null. */
DBCE_SHIM_API const char* dbce_shim_action_name(int32_t index);

/* Opens an ordinary host. folder: an existing folder for the host's own report (host-shutdown.json). profile: the exact
   [Controls] bytes (ini-controls-1). unsupported: "action=reason" lines naming actions the game cannot use (may be null).
   hwnd: the game window (may be null). On success *out is the handle. */
DBCE_SHIM_API int32_t dbce_shim_open(const wchar_t* folder, const char* nonce, const char* profile, int32_t profile_len,
                                     const char* unsupported, void* hwnd, void** out, char* why, int32_t why_len);
/* One Core tick: fills *frame (frame->struct_size must be set). */
DBCE_SHIM_API int32_t dbce_shim_tick(void* handle, dbce_shim_frame* frame);
/* The start report (line, then notes, one per line) and the device table (JSON array), as text. */
DBCE_SHIM_API int32_t dbce_shim_start_report(void* handle, char* text, int32_t len, int32_t* needed);
DBCE_SHIM_API int32_t dbce_shim_device_reports(void* handle, char* text, int32_t len, int32_t* needed);
/* The profile as Core's own parser read it, "key=value" lines: ok (1/0), error, profile, revision, transmission and
   the SHA-256 of the exact bytes. A game adapter takes its gearbox from transmission, never from its own parse. */
DBCE_SHIM_API int32_t dbce_shim_profile_info(void* handle, char* text, int32_t len, int32_t* needed);
/* The host's bounded stop. OK when resolved, DBCE_SHIM_UNRESOLVED otherwise; report receives the reasons. */
DBCE_SHIM_API int32_t dbce_shim_stop(void* handle, char* report, int32_t len, int32_t* needed);

#ifdef __cplusplus
}
#endif
