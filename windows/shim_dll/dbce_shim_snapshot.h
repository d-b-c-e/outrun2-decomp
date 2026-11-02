#pragma once
/* Additive extension 3: the accepted raw-input snapshot of an ordinary handle (valo, sana 124000, for in-F6 binding
 * capture without a second reader). Base ABI 2, its 296-byte frame, the bridge and extras extensions and every existing
 * export are unchanged. A DLL without these exports offers no snapshot: the caller reports binding capture unavailable.
 *
 * - Source: the device reads Core made during the last dbce_shim_tick or dbce_shim_tick_extras, the same reads that
 *   resolved that frame's actions, plus Core's device table (dbce_shim_device_reports' rows). The getter only copies
 *   them: no tick, no device call, no enumeration, open, poll or state read, no file and no force.
 * - Ordinary handles only: dbce_shim_open, dbce_shim_open_extras, or dbce_shim_open2 without DBCE_SHIM_LATCHED.
 *   A latched handle or a copied-input bridge handle gets DBCE_SHIM_REFUSED. Present in the x86 ordinary-only build.
 * - Raw evidence only: axes, buttons and POVs as the device returned them (an ordinary handle injects nothing). The
 *   normalized action values stay in dbce_shim_frame. poll_call and state_call are Core's classification of the two
 *   calls (dbce::shim::Call), not HRESULTs; ordinary reads have no source generation and none is reported.
 * - Devices: only those the profile binds (Core opens no other). Each device row is the table as the frame left it.
 * - Every export is __cdecl; an x86 caller must declare it so. */
#include "dbce_shim.h"
#ifdef __cplusplus
extern "C" {
#endif

#define DBCE_SHIM_SNAPSHOT_ABI 1
#define DBCE_SHIM_SNAPSHOT_MAX_DEVICES 64

enum { DBCE_SHIM_SNAPSHOT_OPENED = 1, DBCE_SHIM_SNAPSHOT_PATH = 2 };   /* dbce_shim_snapshot_device.flags */
/* dbce::shim::Call, the published values. */
enum { DBCE_SHIM_CALL_OK = 0, DBCE_SHIM_CALL_ACQUISITION_LOST = 1, DBCE_SHIM_CALL_GONE = 2, DBCE_SHIM_CALL_NOT_CALLED = 3,
       DBCE_SHIM_CALL_PENDING = 4 };

/* 56 bytes. The caller sets the three size fields; the rest is output. */
typedef struct dbce_shim_snapshot_header {
    uint32_t struct_size;          /* in: sizeof(dbce_shim_snapshot_header) */
    uint32_t abi;                  /* out: DBCE_SHIM_SNAPSHOT_ABI */
    uint32_t device_struct_size;   /* in: sizeof(dbce_shim_snapshot_device) */
    uint32_t sample_struct_size;   /* in: sizeof(dbce_shim_snapshot_sample) */
    uint64_t frame_seq;            /* the dbce_shim_frame.seq these reads belong to */
    int64_t tick_us;               /* when that tick began (Core's clock, microseconds) */
    int64_t boundary_us;           /* its decision time, re-read after the device reads: `stale` is judged at it */
    int64_t fresh_us;              /* a valid read older than this at boundary_us is stale */
    uint32_t device_count;         /* rows in Core's device table: the capacity `devices` needs */
    uint32_t sample_count;         /* reads in that frame: the capacity `samples` needs */
} dbce_shim_snapshot_header;

/* One row of Core's device table, as the frame left it. 192 bytes. */
typedef struct dbce_shim_snapshot_device {
    uint32_t struct_size, abi;
    uint32_t index;                /* the table row: the profile's planning order (an extras handle's own devices after) */
    uint32_t epoch;                /* the open epoch: current when opened, otherwise the last one (0: never opened) */
    uint32_t flags;                /* DBCE_SHIM_SNAPSHOT_OPENED: the epoch is open after this frame;
                                      DBCE_SHIM_SNAPSHOT_PATH: an owned interface path exists (text in dbce_shim_device_reports) */
    uint32_t axis_mask, range_mask, pov_mask;   /* caps read back after the open; all zero unless OPENED */
    uint8_t button_mask[16];
    int32_t minimum[8], maximum[8];
    char instance[40], product[40];   /* GUID text as Core holds it: lower case, braced, NUL-terminated (the profile's dev=/prod=) */
} dbce_shim_snapshot_device;

/* One device read of that frame. 224 bytes. */
typedef struct dbce_shim_snapshot_sample {
    uint32_t struct_size, abi;
    uint32_t index;                /* the device table row */
    uint32_t epoch;                /* the epoch this read belongs to */
    uint64_t device_seq;           /* that device's read sequence: increases by one per read, across epochs */
    int64_t read_start_us, read_end_us;
    uint8_t poll_call, state_call; /* DBCE_SHIM_CALL_*: the state call is NOT_CALLED unless the poll was OK */
    uint8_t valid;                 /* both calls OK: the values below are a complete state read */
    uint8_t stale;                 /* valid, but older than fresh_us at boundary_us: Core did not use it */
    uint8_t ended;                 /* this read ended its epoch: the device row is no longer OPENED */
    uint8_t reserved[3];
    int32_t axes[8];               /* in the device's read-back range (the device row's minimum/maximum) */
    uint8_t buttons[128];          /* DirectInput bytes: down when 0x80 is set */
    uint32_t pov[4];               /* hundredths of a degree, 0xFFFFFFFF centred */
} dbce_shim_snapshot_sample;

DBCE_SHIM_API int32_t dbce_shim_snapshot_abi_version(void);
/* The three struct sizes; any pointer may be null. */
DBCE_SHIM_API int32_t dbce_shim_snapshot_sizes(int32_t* header, int32_t* device, int32_t* sample);
/* Owner thread. Copies the reads of frame_seq, which must be the frame the last successful tick returned: another seq or
   no successful tick yet gives DBCE_SHIM_STALE and writes nothing.
   - A tick that returns DBCE_SHIM_REFUSED (Core may have ticked) leaves no snapshot: STALE until the next successful tick.
   - A tick refused before Core ticks (DBCE_SHIM_BAD_ARGUMENT: a bad frame, a refused context mask, the wrong tick for
     the handle; or DBCE_SHIM_WRONG_THREAD) changes nothing: the last successful frame and its snapshot stay as they were.
   When the seq matches, the header is filled even if a
   capacity is short; then DBCE_SHIM_BAD_ARGUMENT, and neither array is written (all or nothing). A null array is
   allowed with a zero capacity. A device table above DBCE_SHIM_SNAPSHOT_MAX_DEVICES is never truncated: DBCE_SHIM_REFUSED,
   nothing written; the handle's ordinary ticks are unaffected.
   Repeated calls for the same frame give identical results and call no device: the same seq is the same observation,
   never a newer one. `stale` is Core's verdict at that frame's boundary_us, not at the call; no clock is read here. A
   caller takes the snapshot right after the tick that returned frame_seq and treats an unchanged device_seq as no new read.
   A sample whose epoch the device row no longer has open is `ended`, whatever its calls say (a pending read past Core's
   limit ends its epoch with both calls still PENDING). */
DBCE_SHIM_API int32_t dbce_shim_snapshot(void* handle, uint64_t frame_seq, dbce_shim_snapshot_header* header,
                                         dbce_shim_snapshot_device* devices, int32_t device_capacity,
                                         dbce_shim_snapshot_sample* samples, int32_t sample_capacity);

#ifdef __cplusplus
}
#endif
