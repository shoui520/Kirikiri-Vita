#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace krkrvita {

// Assemble the exact CRLF-delimited string delivered by KAG's onScript
// callback. Yuri historically appended every line to a growing ttstr, causing
// hundreds of reallocations for large retail configuration blocks. This
// performs two read-only passes and one bounded allocation.
template <typename Char, typename LineAt>
std::basic_string<Char> assemble_kag_inline_script(
    std::size_t first, std::size_t last, LineAt&& line_at) {
    static_assert(std::is_integral_v<Char>);
    if (last < first) throw std::invalid_argument("invalid KAG script range");

    std::size_t total = 0;
    for (std::size_t line = first; line < last; ++line) {
        const Char* text = line_at(line);
        const auto length = text ? std::char_traits<Char>::length(text) : 0;
        if (length > std::numeric_limits<std::size_t>::max() - total - 2)
            throw std::length_error("KAG inline script is too large");
        total += length + 2;
    }

    std::basic_string<Char> result;
    if (total > result.max_size())
        throw std::length_error("KAG inline script exceeds string capacity");
    result.reserve(total);
    for (std::size_t line = first; line < last; ++line) {
        const Char* text = line_at(line);
        if (text) result.append(text);
        result.push_back(static_cast<Char>('\r'));
        result.push_back(static_cast<Char>('\n'));
    }
    return result;
}

} // namespace krkrvita
