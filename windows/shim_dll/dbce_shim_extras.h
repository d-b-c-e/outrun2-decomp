#pragma once
/* Additive extension 2: a game's declared extra actions beside the 31 (hub artifacts/extra-actions-design-20261014, with
 * the sana and deli reviews). Base ABI 2, its 296-byte frame, the 31 actions and every existing export are unchanged, and
 * a handle opened without a declaration behaves exactly as before. Ordinary input only: no latched control, injection or
 * capture of extras in this version, so no recording claims to hold them.
 *
 * - Declaration: ASCII text, at most 4096 bytes, passed with its length and copied during the open:
 *     game <id>                                  first; [a-z0-9-], 1-32
 *     contexts observed|advisory <name>...       optional, second; at most 8; bit i of a tick's mask = name i
 *     <game>.<name> digital|pedal|centred [contexts=a,b]
 *     <game>.<name> stick circle|square [contexts=a,b]   two slots, X then Y
 *   At most 32 scalar slots, in declaration order.
 * - Profile: dbce-controls.ini's [Extras] (Schema, Game, Declaration = the declaration's SHA-256, optional Revision),
 *   read only for a handle opened with a declaration. Pinned DLLs ignore the section.
 * - One tick: an extras handle advances only through dbce_shim_tick_extras (dbce_shim_tick refuses it), which reads the
 *   devices once for the common frame and the extras. dbce_shim_extras only reads that tick's cached frame.
 * - Statuses: dbce::shim::Status, plus the appended 14 (ContextInactive), which only extras frames carry.
 * - Every export is __cdecl; an x86 caller must declare it so. Present in the x86 ordinary-only build too. */
#include "dbce_shim.h"
#ifdef __cplusplus
extern "C" {
#endif

#define DBCE_SHIM_EXTRAS_ABI 1
#define DBCE_SHIM_MAX_EXTRA_SLOTS 32
#define DBCE_SHIM_MAX_DECLARATION 4096
#define DBCE_SHIM_CONTEXT_INACTIVE 14

/* 296 bytes, no pointers: the same on x86 and x64. slot[i] is scalar slot i; dbce_shim_extras_info maps each id to its
   first slot and component count. */
typedef struct dbce_shim_extras_frame {
    uint32_t struct_size;          /* in: sizeof(dbce_shim_extras_frame); must equal dbce_shim_extras_frame_size() */
    uint32_t abi;                  /* out: DBCE_SHIM_EXTRAS_ABI */
    uint64_t seq;                  /* the dbce_shim_frame.seq of the same tick */
    uint64_t down, up;             /* digital slots' edges against the previous tick, bit = slot index */
    uint32_t count;                /* scalar slots in use */
    uint32_t contexts;             /* the mask this tick resolved with */
    dbce_shim_action slot[DBCE_SHIM_MAX_EXTRA_SLOTS];
} dbce_shim_extras_frame;

DBCE_SHIM_API int32_t dbce_shim_extras_abi_version(void);
DBCE_SHIM_API int32_t dbce_shim_extras_frame_size(void);
/* dbce_shim_open (version 1, scalar arguments) plus the declaration. A null declaration or a zero length is exactly
   dbce_shim_open. A malformed declaration refuses the open (DBCE_SHIM_BAD_ARGUMENT, the reason in why); the adapter may
   then open the common route with dbce_shim_open and must report its extras failed, never delivered. */
DBCE_SHIM_API int32_t dbce_shim_open_extras(const wchar_t* folder, const char* nonce, const char* profile, int32_t profile_len,
                                            const char* unsupported, void* hwnd, const char* declaration, int32_t declaration_len,
                                            void** out, char* why, int32_t why_len);
/* The one tick of an extras handle (owner thread): fills both frames (struct_size set in each). Mask bits beyond the
   declaration's contexts, or any bit on an advisory or context-free declaration: DBCE_SHIM_BAD_ARGUMENT, nothing advances.
   An observed adapter that cannot tell the game's context passes 0, never every bit. */
DBCE_SHIM_API int32_t dbce_shim_tick_extras(void* handle, uint32_t active_contexts, dbce_shim_frame* frame, dbce_shim_extras_frame* extras);
/* Read-only: the extras frame the last dbce_shim_tick_extras made, by its seq (another seq, or no tick yet:
   DBCE_SHIM_STALE). Repeated reads are identical, including the edges. */
DBCE_SHIM_API int32_t dbce_shim_extras(void* handle, uint64_t frame_seq, dbce_shim_extras_frame* out);
/* The declaration as Core planned it (JSON): acceptedSchema (the highest [Extras] Schema this open reads: 1 for the
   ABI, so a writer never emits Schema 2 or cal= here), game, declaration SHA-256, context mode and names, the [Extras] Revision,
   ignored keys, and per extra its shape, first slot, components, contexts, plan and reason, the last tick's slot statuses,
   and each bound input. The usual text and needed contract; callers must reject truncation. */
DBCE_SHIM_API int32_t dbce_shim_extras_info(void* handle, char* text, int32_t len, int32_t* needed);

#ifdef __cplusplus
}
#endif
