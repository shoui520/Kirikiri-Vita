#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace krkrvita {

inline constexpr std::size_t kVitaBitmapMemblockThreshold =
    1u * 1024u * 1024u;
inline constexpr std::size_t kVitaBitmapMemblockBudget =
    64u * 1024u * 1024u;
inline constexpr std::size_t kVitaBitmapMemblockPage = 4096u;
inline constexpr std::size_t kVitaBitmapAllocationHeaderBytes = 32u;
inline constexpr std::size_t kVitaBitmapPayloadAlignment = 16u;

constexpr bool vita_bitmap_uses_memblock(std::size_t requested) {
    return requested >= kVitaBitmapMemblockThreshold;
}

constexpr std::size_t vita_bitmap_memblock_bytes(std::size_t requested) {
    constexpr std::size_t overhead = kVitaBitmapAllocationHeaderBytes +
                                     kVitaBitmapPayloadAlignment - 1;
    if (requested > std::numeric_limits<std::size_t>::max() - overhead)
        return 0;
    const std::size_t total = requested + overhead;
    if (total > std::numeric_limits<std::size_t>::max() -
                    (kVitaBitmapMemblockPage - 1))
        return 0;
    return (total + kVitaBitmapMemblockPage - 1) &
           ~(kVitaBitmapMemblockPage - 1);
}

constexpr bool vita_bitmap_memblock_budget_allows(std::size_t live,
                                                   std::size_t requested) {
    const std::size_t mapped = vita_bitmap_memblock_bytes(requested);
    return mapped != 0 && live <= kVitaBitmapMemblockBudget &&
           mapped <= kVitaBitmapMemblockBudget - live;
}

void* vita_bitmap_allocate(std::size_t size);
void vita_bitmap_deallocate(void* memory) noexcept;
std::uint64_t vita_bitmap_memblock_bytes_live() noexcept;

} // namespace krkrvita
