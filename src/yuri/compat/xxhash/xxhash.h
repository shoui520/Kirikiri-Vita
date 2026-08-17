#pragma once

#include <cstddef>
#include <cstdint>

// XXH32-compatible implementation used by Yuri's optional half-line texture
// cache.  It is kept header-only because the Vita backend does not otherwise
// need an xxHash library.
static inline std::uint32_t XXH32_rotl(std::uint32_t value, int count) {
    return (value << count) | (value >> (32 - count));
}

static inline std::uint32_t XXH32_read32(const void* source) {
    const auto* p = static_cast<const std::uint8_t*>(source);
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

static inline std::uint32_t XXH32_round(std::uint32_t accumulator,
                                        std::uint32_t input) {
    accumulator += input * 2246822519u;
    accumulator = XXH32_rotl(accumulator, 13);
    return accumulator * 2654435761u;
}

static inline std::uint32_t XXH32(const void* input, std::size_t length,
                                  std::uint32_t seed) {
    const auto* p = static_cast<const std::uint8_t*>(input);
    const auto* end = p + length;
    std::uint32_t hash;
    if (length >= 16) {
        std::uint32_t v1 = seed + 2654435761u + 2246822519u;
        std::uint32_t v2 = seed + 2246822519u;
        std::uint32_t v3 = seed;
        std::uint32_t v4 = seed - 2654435761u;
        const auto* limit = end - 16;
        do {
            v1 = XXH32_round(v1, XXH32_read32(p)); p += 4;
            v2 = XXH32_round(v2, XXH32_read32(p)); p += 4;
            v3 = XXH32_round(v3, XXH32_read32(p)); p += 4;
            v4 = XXH32_round(v4, XXH32_read32(p)); p += 4;
        } while (p <= limit);
        hash = XXH32_rotl(v1, 1) + XXH32_rotl(v2, 7) +
               XXH32_rotl(v3, 12) + XXH32_rotl(v4, 18);
    } else {
        hash = seed + 374761393u;
    }
    hash += static_cast<std::uint32_t>(length);
    while (p + 4 <= end) {
        hash += XXH32_read32(p) * 3266489917u;
        hash = XXH32_rotl(hash, 17) * 668265263u;
        p += 4;
    }
    while (p < end) {
        hash += *p++ * 374761393u;
        hash = XXH32_rotl(hash, 11) * 2654435761u;
    }
    hash ^= hash >> 15;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;
    return hash;
}
