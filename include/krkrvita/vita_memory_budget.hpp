#pragma once

#include <cstddef>

namespace krkrvita {

// VitaSDK's newlib heap is one fixed USER_RW memblock allocated before main;
// free() can reuse it but cannot return any part of it to the kernel. Reserve
// the upper 64 MiB for independently reclaimable large Kirikiri bitmaps while
// keeping the total newlib-plus-VitaGL-threshold reservation unchanged from
// the previously hardware-tested 192 MiB + 16 MiB split. VitaGL therefore
// retains the same pool budget. Large CPU surfaces prefer that reclaimable
// tier; retail projects whose live layer set exceeds 64 MiB spill into this
// fixed newlib heap instead of treating the memblock budget as a hard limit.
inline constexpr std::size_t kVitaNewlibHeapBytes = 128u * 1024u * 1024u;
inline constexpr int kVitaGlApplicationRamThresholdBytes =
    80 * 1024 * 1024;
inline constexpr std::size_t kVitaGlNonBitmapHeadroomBytes =
    16u * 1024u * 1024u;

// A full 1280x960 32-bit Kirikiri bitmap plus tTVPBitmapBitsAlloc metadata.
// This remains a useful budget invariant, but it is not a VitaGL-pool probe:
// decoded CPU bitmaps are ordinary application allocations.
constexpr std::size_t bitmap_allocation_bytes(std::size_t width,
                                               std::size_t height) {
    return width * height * 4 + 40;
}

inline constexpr std::size_t kRetailBitmapAllocationBytes =
    bitmap_allocation_bytes(1280, 960);

} // namespace krkrvita
