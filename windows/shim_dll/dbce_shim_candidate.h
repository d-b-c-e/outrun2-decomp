#pragma once
/* Additive extension 4: one explicitly selected capture candidate on an ordinary handle, so an in-game editor can bind a
 * first device while the profile binds none (shim_dll/README.md, extension 4). Base ABI 2, its 296-byte frame, extensions
 * 1-3 and every existing export are unchanged: Core's device table stays the profile's, and extension 3's snapshot never
 * lists a candidate. A DLL without these exports offers no first-device route.
 *
 * - Explicit only. Nothing is enumerated or opened until dbce_shim_candidate_list or dbce_shim_candidate_select; both
 *   only queue the request for the host's existing provider worker, so no call here waits on a driver.
 * - One shared reader. The worker lists and opens through the same DirectInput provider as Core (background,
 *   non-exclusive, no force-feedback call). The ordinary tick reads an open candidate once, after Core's own reads. A
 *   candidate never resolves an action, never appears in dbce_shim_frame, extension 3, the journal or the recorder.
 * - One at a time. A select replaces the previous candidate. An instance the profile binds is refused: extension 3
 *   already offers it.
 * - Release: dbce_shim_candidate_release; a read that ends its epoch (lost, gone, or pending past Core's limit: select
 *   again); no dbce_shim_candidate_snapshot of the current frame for 2 s; dbce_shim_stop, whose existing provider
 *   shutdown releases any handle still open.
 * - Ordinary handles only (dbce_shim_open, dbce_shim_open_extras, dbce_shim_open2 without DBCE_SHIM_LATCHED). A latched
 *   or copied-input bridge handle gets DBCE_SHIM_REFUSED. Present in the x86 ordinary-only build. Owner thread only.
 * - Every export is __cdecl; an x86 caller must declare it so. */
#include "dbce_shim_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif

#define DBCE_SHIM_CANDIDATE_ABI 1

DBCE_SHIM_API int32_t dbce_shim_candidate_abi_version(void);
/* Queues one listing of the game controllers attached now. The result appears in dbce_shim_candidate_report after a
   later tick ("listing.seq" advances; "listing.pending" is true until then). */
DBCE_SHIM_API int32_t dbce_shim_candidate_list(void* handle);
/* Selects one controller by the GUID text of a listing row: lower case, braced, as dbce_shim_device_reports shows it.
   Replaces any candidate and queues its open. DBCE_SHIM_BAD_ARGUMENT for malformed text, DBCE_SHIM_REFUSED for a
   profile-bound instance; `why` (optional) says which. Attachment and identity are checked by the worker at the open. */
DBCE_SHIM_API int32_t dbce_shim_candidate_select(void* handle, const char* instance, const char* product, char* why, int32_t why_len);
/* Releases the candidate (its handle is closed by the worker). OK when there is none. */
DBCE_SHIM_API int32_t dbce_shim_candidate_release(void* handle);
/* JSON as of the last tick: {"abi":1,"listing":{"seq":N,"pending":B,"devices":[{"instance","product","name","bound"}]},
   "candidate":{"state":"none|opening|open|lost|refused","instance","product","name","interfacePath","epoch":N,
   "opened":B,"reason"}}. "bound" marks instances the profile binds. Text as dbce_shim_device_reports. */
DBCE_SHIM_API int32_t dbce_shim_candidate_report(void* handle, char* text, int32_t len, int32_t* needed);
/* The candidate in extension 3's structs, for the tick that returned frame_seq (another seq: DBCE_SHIM_STALE, nothing
   written). device_count is 0 with no candidate, else 1: row index 0, the current or last epoch, OPENED while open
   (masks and ranges only then), PATH when an interface path is known. sample_count is 1 when this frame read it: its
   epoch, device_seq, both calls, valid, stale 0 (read this frame), ended when the read ended the epoch, and the raw state.
   header.boundary_us is that read's end (the tick's start without one); fresh_us is the ordinary bound. Capacity rules as
   dbce_shim_snapshot. A matching call renews the candidate's lifetime; it calls no device and reads no clock. */
DBCE_SHIM_API int32_t dbce_shim_candidate_snapshot(void* handle, uint64_t frame_seq, dbce_shim_snapshot_header* header,
                                                   dbce_shim_snapshot_device* device, int32_t device_capacity,
                                                   dbce_shim_snapshot_sample* sample, int32_t sample_capacity);

#ifdef __cplusplus
}
#endif
