#pragma once

#include <cstddef>

namespace krkrvita {

// VitaSDK's newlib heap is one fixed USER_RW memblock allocated before main;
// free() can reuse it but cannot return any part of it to the kernel. It backs
// scripts, TJS objects, SQLite, FreeType and every bitmap below the memblock
// threshold. Large Kirikiri surfaces deliberately do not live here: they use
// independently reclaimable USER_RW memblocks instead.
inline constexpr std::size_t kVitaNewlibHeapBytes = 128u * 1024u * 1024u;

// vglInitExtended's fourth parameter is the free USER_RW the application keeps,
// not the size of VitaGL's pool: VitaGL claims everything above it. Verified in
// vitaGL's own vgl.c, where vglInitWithCustomThreshold sizes the pool as
// `info.size_user > ram_threshold ? info.size_user - ram_threshold : 0`.
//
// Passing a fixed threshold therefore hands VitaGL every byte the app did not
// reserve in advance. With ATTRIBUTE2=12 the app has roughly 365 MiB of
// USER_RW, so an 80 MiB threshold left VitaGL holding well over 100 MiB while
// this build uses it only to present a handful of 960x544 textures. Retail
// 1280x720 projects then ran out of large-bitmap memory with most of the
// console idle.
//
// vglInitExtended also passes cdram_threshold = 0, so VitaGL already owns all
// 112 MiB of CDRAM for its textures. Its USER_RW pool only has to cover what
// cannot live there, which is why a presenter-sized figure is sufficient.
//
// Size VitaGL's pool for what it actually does and derive the threshold from
// the memory that really is free at initialization.
inline constexpr std::size_t kVitaGlPoolBytes = 48u * 1024u * 1024u;

// Never leave the application less than this, however little the kernel
// reports free; and fall back to it when the query fails.
inline constexpr int kVitaGlMinApplicationRamThresholdBytes =
    64 * 1024 * 1024;

// Free USER_RW that large bitmaps must never consume. Non-bitmap subsystems
// still take memblocks after startup (audio, movie, font and VitaGL growth),
// and a failed allocation there is not recoverable the way a bitmap is.
inline constexpr std::size_t kVitaBitmapMemblockReserveBytes =
    32u * 1024u * 1024u;

// The value to pass to vglInitExtended given the free USER_RW measured just
// before the call. Leaves the application everything except VitaGL's pool.
constexpr int vitagl_application_ram_threshold(std::size_t free_user_bytes) {
    if (free_user_bytes <= kVitaGlPoolBytes)
        return kVitaGlMinApplicationRamThresholdBytes;
    const std::size_t remaining = free_user_bytes - kVitaGlPoolBytes;
    if (remaining <=
        static_cast<std::size_t>(kVitaGlMinApplicationRamThresholdBytes))
        return kVitaGlMinApplicationRamThresholdBytes;
    return static_cast<int>(remaining);
}

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
