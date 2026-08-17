#pragma once

#include <cstdint>

namespace krkrvita {

// Joins positive partial reads until the requested byte count is satisfied.
// Zero remains the normal EOF signal.  An error after a valid prefix returns
// that prefix, matching the ordinary read(2) contract without exposing an
// uninitialized tail to callers which correctly honor the returned count.
template <typename Reader>
std::uint32_t read_all_bytes(std::uint32_t size, Reader&& reader) {
    std::uint32_t total = 0;
    while (total < size) {
        const std::uint32_t remaining = size - total;
        const std::int64_t read =
            static_cast<std::int64_t>(reader(total, remaining));
        if (read <= 0 || static_cast<std::uint64_t>(read) > remaining) {
            break;
        }
        total += static_cast<std::uint32_t>(read);
    }
    return total;
}

// The archive reader frequently wraps a stream in ReadBuffer(), whose base
// implementation deliberately treats one short Read() as an error.  Vita's
// file manager is allowed to return a positive short count for a regular
// file, however, so use the same bounded join at this layer as well.  Keeping
// this helper generic lets the generated Yuri source use it without exposing
// any Vita or Yuri types from this small header.
template <typename Stream>
std::uint32_t read_all_stream(Stream& stream, void* buffer,
                              std::uint32_t size) {
    auto* bytes = static_cast<std::uint8_t*>(buffer);
    return read_all_bytes(size,
        [&stream, bytes](std::uint32_t offset, std::uint32_t remaining) {
            return stream.Read(bytes + offset, remaining);
        });
}

} // namespace krkrvita
