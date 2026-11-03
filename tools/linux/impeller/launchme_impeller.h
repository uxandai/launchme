#pragma once
// Launchme's versioned addition to the pinned Impeller standalone C ABI.
#include "impeller.h"

#define LAUNCHME_IMPELLER_ABI 1u
IMPELLER_EXTERN_C_BEGIN
typedef enum LaunchmeImpellerFormat {
    kLaunchmeImpellerRGBA = 0,
    kLaunchmeImpellerBGRA = 1,
    kLaunchmeImpellerBC1 = 2,
    kLaunchmeImpellerBC3 = 3,
} LaunchmeImpellerFormat;

IMPELLER_EXPORT uint32_t LaunchmeImpellerGetAbiVersion(void);
IMPELLER_EXPORT bool LaunchmeImpellerSupportsFormat(ImpellerContext context, LaunchmeImpellerFormat format);
// Tightly packed premultiplied pixels or complete 4x4 BC blocks, one mip level.
// Copies the supplied memory before returning. Invalid sizes/formats/lengths return NULL.
IMPELLER_EXPORT ImpellerTexture LaunchmeImpellerTextureNew(ImpellerContext context, LaunchmeImpellerFormat format,
    int32_t width, int32_t height, const uint8_t* data, uint64_t length);
// Renders a display list to an RGBA8 offscreen texture; owns one returned reference.
IMPELLER_EXPORT ImpellerTexture LaunchmeImpellerRenderToTextureNew(ImpellerContext context, ImpellerDisplayList list,
    int32_t width, int32_t height);
// Synchronous readback of RGBA8/BGRA8 into tightly packed bytes. For captures, not presentation.
IMPELLER_EXPORT bool LaunchmeImpellerTextureRead(ImpellerContext context, ImpellerTexture texture,
    uint8_t* destination, uint64_t length);
IMPELLER_EXTERN_C_END
