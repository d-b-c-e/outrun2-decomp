/* Experimental external-owner ABI v1. Fixed layout, Cdecl, no implicit bools.
 * Native 0.10.0. This is NOT a guarantee of physical stop through driver failure.
 * Handles below must be valid in the calling owner process, not copied over IPC.
 * The consumer verifies game build/hash and authenticates its IPC peers.
 */
#ifndef DBCE_EXTERNAL_FFB_H
#define DBCE_EXTERNAL_FFB_H
#include <stdint.h>
#include <stddef.h>

enum ExternalFfbResult {
    EXT_OK=1, EXT_WAITING=2, EXT_APPLIED=3, EXT_CANCELED=4, EXT_CLOSED=5, EXT_PENDING=6,
    EXT_INVALID=-1, EXT_WRONG_MODE=-2, EXT_WRONG_SESSION=-3, EXT_STALE=-4,
    EXT_BUSY=-5, EXT_FAULT=-6, EXT_INELIGIBLE=-7, EXT_UNAVAILABLE=-8
};
enum ExternalFfbState {
    EXT_OWNER_ONLY=0, EXT_PREPARED=1, EXT_ARMING=2, EXT_ARMED=3,
    EXT_REVOKED=4, EXT_SESSION_CLOSED=5, EXT_FAULTED=6
};
enum ExternalFfbReason {
    EXT_REASON_NONE=0, EXT_REASON_STOP=1, EXT_REASON_FOCUS=2, EXT_REASON_TARGET_EXIT=3,
    EXT_REASON_CLIENT_EXIT=4, EXT_REASON_PRODUCER_EXIT=5, EXT_REASON_SOURCE_STALE=6,
    EXT_REASON_CLIENT_STALE=7, EXT_REASON_INPUT=8, EXT_REASON_WINDOW=9,
    EXT_REASON_DRIVER=10, EXT_REASON_SOURCE_INVALID=11, EXT_REASON_CLIENT_OFF=12,
    EXT_REASON_CLOSE=13, EXT_REASON_LEGACY_CANCEL=14, EXT_REASON_CLOCK=15
};
enum ExternalFfbCancellation { EXT_CANCEL_NONE=0, EXT_CANCEL_REQUESTED=1, EXT_CANCEL_API_DONE=2, EXT_CANCEL_API_FAILED=3 };
enum ExternalFfbStatusFlags { EXT_MODE_ENTERED=1, EXT_OWNER_VALID=2, EXT_DRIVER_BUSY=4, EXT_PHYSICAL_UNVERIFIED=8, EXT_CLOSE_PENDING=16 };
enum ExternalFfbAttestations { EXT_LOCAL_PLAYER=1, EXT_DRIVING=2, EXT_KNOWN_MODE=4, EXT_REQUIRED_INPUTS=8, EXT_ALL_ATTESTATIONS=15 };
enum ExternalFfbClientFlags { EXT_CLIENT_ENABLED=1, EXT_CLIENT_INPUTS_VALID=2, EXT_ALL_CLIENT_FLAGS=3 };

#pragma pack(push, 8)
typedef struct ExternalProcessV1 {
    uint64_t handle, creationFileTime;
    uint32_t pid, reserved;
} ExternalProcessV1;
typedef struct ExternalFfbConfigV1 {
    uint32_t cbSize, version;
    ExternalProcessV1 target, client, producer;
    unsigned char wheelGuid[16];
    uint32_t maxMagnitude, sourceMaxAgeMs, clientMaxAgeMs, pulseMaxMs;
} ExternalFfbConfigV1;
typedef struct ExternalFfbArmTicketV1 {
    uint32_t cbSize, version;
    uint64_t generation, nonceLow, nonceHigh;
    int64_t issuedQpc, expiresQpc, qpcFrequency;
} ExternalFfbArmTicketV1;
typedef struct ExternalFfbClientFrameV1 {
    uint32_t cbSize, flags;
    uint64_t generation, sequence;
    int64_t capturedQpc, qpcFrequency;
} ExternalFfbClientFrameV1;
typedef struct ExternalFfbForceFrameV1 {
    uint32_t cbSize, attestations;
    uint64_t generation, nonceLow, nonceHigh, sequence;
    int64_t capturedQpc, qpcFrequency;
    int32_t signedMagnitude;
    uint32_t reserved;
} ExternalFfbForceFrameV1;
typedef struct ExternalFfbStatusV1 {
    uint32_t cbSize, version;
    uint64_t session, generation;
    uint32_t state, reason, cancellation, flags;
    uint64_t sourceSequence, clientSequence;
    int64_t sourceDeadlineQpc, clientDeadlineQpc, wheelReadQpc, lastStartQpc;
    int32_t driverHResult;
    uint32_t reserved;
    int64_t qpcFrequency;
} ExternalFfbStatusV1;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(ExternalProcessV1)==24, "process ABI");
static_assert(sizeof(ExternalFfbConfigV1)==112 && offsetof(ExternalFfbConfigV1,wheelGuid)==80, "config ABI");
static_assert(offsetof(ExternalFfbConfigV1,maxMagnitude)==96, "config limits ABI");
static_assert(sizeof(ExternalFfbArmTicketV1)==56 && offsetof(ExternalFfbArmTicketV1,issuedQpc)==32, "ticket ABI");
static_assert(sizeof(ExternalFfbClientFrameV1)==40 && offsetof(ExternalFfbClientFrameV1,capturedQpc)==24, "client ABI");
static_assert(sizeof(ExternalFfbForceFrameV1)==64 && offsetof(ExternalFfbForceFrameV1,capturedQpc)==40, "force ABI");
static_assert(sizeof(ExternalFfbStatusV1)==104 && offsetof(ExternalFfbStatusV1,driverHResult)==88, "status ABI");
#endif
#endif
