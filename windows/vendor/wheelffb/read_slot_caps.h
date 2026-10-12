/* GetReadSlotCaps's result (native 0.11.0; see wheelffb.h). Fixed layout, no pointers: the same on x86 and x64. */
#ifndef DBCE_READ_SLOT_CAPS_H
#define DBCE_READ_SLOT_CAPS_H
#include <stdint.h>
#include <stddef.h>

#pragma pack(push, 8)
typedef struct ReadSlotCapsV1 {
    uint32_t cbSize, version;
    unsigned char instanceGuid[16], productGuid[16];   /* GUID bytes, as GetDeviceInfo returns them */
    int32_t axisPresent[8], rangeOk[8], rangeMin[8], rangeMax[8];
    unsigned char buttonPresent[128];
    int32_t povPresent[4];
    char interfacePath[1024];
} ReadSlotCapsV1;
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(ReadSlotCapsV1)==1336 && offsetof(ReadSlotCapsV1,axisPresent)==40 && offsetof(ReadSlotCapsV1,buttonPresent)==168 &&
              offsetof(ReadSlotCapsV1,interfacePath)==312, "read slot caps ABI");
#endif
#endif
