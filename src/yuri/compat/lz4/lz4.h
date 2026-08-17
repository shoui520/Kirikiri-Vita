#pragma once

#include <cstring>

// Yuri's optional software texture cache uses LZ4 only as an internal memory
// optimization.  Vita fixes that option to "none"; these lossless raw-copy
// fallbacks retain linkable behavior if one of those classes is instantiated.
static inline int LZ4_compress_default(const char* source, char* destination,
                                       int source_size,
                                       int destination_capacity) {
    if (!source || !destination || source_size < 0 ||
        destination_capacity < source_size)
        return 0;
    std::memcpy(destination, source, static_cast<unsigned int>(source_size));
    return source_size;
}

static inline int LZ4_decompress_fast(const char* source, char* destination,
                                      int original_size) {
    if (!source || !destination || original_size < 0) return -1;
    std::memcpy(destination, source, static_cast<unsigned int>(original_size));
    return original_size;
}
