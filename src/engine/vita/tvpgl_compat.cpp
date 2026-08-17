#include "tjsCommHead.h"
#include "tvpgl.h"

extern "C" void TVPGL_ASM_Init();

// These two channel-swap entry points were added after Yuri's generated TVPGL
// core.  krkrz's TVPGL_C_Init installs their implementations before graphics
// loading begins; this unit only supplies the pointer storage omitted by Yuri.
extern "C" {
// Yuri CPU ABI: TVP_CPU_HAS_NEON (0x02000000) | TVP_CPU_FAMILY_ARM (3).
// This must never be aliased to krkrz's TVPCPUType/TVPCPUFeatures: the same
// bit pattern means SSE3 + MIPS to that newer ABI.
tjs_uint32 TVPYuriCPUFeatures = 0x02000003u;
void (*TVPRedBlueSwap)(tjs_uint32 *dest, tjs_int len) = nullptr;
void (*TVPRedBlueSwapCopy)(tjs_uint32 *dest, const tjs_uint32 *src,
                           tjs_int len) = nullptr;
}

void krkrvita_initialize_yuri_tvpgl() {
    TVPGL_ASM_Init();
}
