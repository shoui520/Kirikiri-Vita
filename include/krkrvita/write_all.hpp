#pragma once

#include <cstdint>

namespace krkrvita {

// A successful write may consume fewer bytes than requested. Keep advancing
// until the complete request is written, or return the exact completed prefix
// when the backend reports an error, EOF-like zero, or an invalid byte count.
// The caller can then preserve its native short-write/error semantics.
template <typename Writer>
std::uint32_t write_all_bytes(std::uint32_t size, Writer&& writer) {
    std::uint32_t total = 0;
    while (total < size) {
        const std::uint32_t remaining = size - total;
        const std::int64_t written =
            static_cast<std::int64_t>(writer(total, remaining));
        if (written <= 0 ||
            static_cast<std::uint64_t>(written) > remaining) {
            break;
        }
        total += static_cast<std::uint32_t>(written);
    }
    return total;
}

} // namespace krkrvita
