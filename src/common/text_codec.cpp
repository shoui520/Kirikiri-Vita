#include "krkrvita/text_codec.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

#include <zlib.h>

namespace krkrvita {
namespace {

std::uint64_t u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("truncated Kirikiri text header");
    }
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(bytes[offset + i]) << (i * 8);
    return value;
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::string utf16le(std::span<const std::uint8_t> bytes, int cipher) {
    if (bytes.size() % 2) throw std::runtime_error("UTF-16 Kirikiri text has an odd size");
    std::string output;
    output.reserve(bytes.size());
    for (std::size_t position = 0; position < bytes.size(); position += 2) {
        std::uint16_t value = static_cast<std::uint16_t>(bytes[position]) |
                              static_cast<std::uint16_t>(bytes[position + 1]) << 8;
        if (cipher == 0 && value >= 0x20) {
            value ^= static_cast<std::uint16_t>(((value & 0xfe) << 8) ^ 1);
        } else if (cipher == 1) {
            value = static_cast<std::uint16_t>(((value & 0xaaaa) >> 1) |
                                               ((value & 0x5555) << 1));
        }
        std::uint32_t codepoint = value;
        if (value >= 0xd800 && value <= 0xdbff) {
            position += 2;
            if (position >= bytes.size()) throw std::runtime_error("truncated UTF-16 surrogate");
            auto low = static_cast<std::uint16_t>(bytes[position]) |
                       static_cast<std::uint16_t>(bytes[position + 1]) << 8;
            if (cipher == 0 && low >= 0x20) {
                low ^= static_cast<std::uint16_t>(((low & 0xfe) << 8) ^ 1);
            } else if (cipher == 1) {
                low = static_cast<std::uint16_t>(((low & 0xaaaa) >> 1) |
                                                 ((low & 0x5555) << 1));
            }
            if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("invalid UTF-16 surrogate");
            codepoint = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
        } else if (value >= 0xdc00 && value <= 0xdfff) {
            throw std::runtime_error("invalid UTF-16 surrogate");
        }
        if (codepoint) append_utf8(output, codepoint);
    }
    return output;
}

} // namespace

bool decode_kirikiri_text(std::span<const std::uint8_t> bytes,
                          std::string& utf8, std::string* error) {
    try {
        utf8.clear();
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
            utf8.assign(reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
            return true;
        }
        if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
            utf8 = utf16le(bytes.subspan(2), -1);
            return true;
        }
        if (bytes.size() >= 5 && bytes[0] == 0xfe && bytes[1] == 0xfe) {
            const auto mode = bytes[2];
            if (bytes[3] != 0xff || bytes[4] != 0xfe || mode > 2) {
                throw std::runtime_error("unsupported Kirikiri text cipher header");
            }
            if (mode < 2) {
                utf8 = utf16le(bytes.subspan(5), mode);
                return true;
            }
            if (bytes.size() < 21) throw std::runtime_error("truncated compressed text header");
            const auto compressed_size = u64(bytes, 5);
            const auto original_size = u64(bytes, 13);
            if (compressed_size != bytes.size() - 21 || original_size > 64 * 1024 * 1024 ||
                original_size > std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("invalid compressed Kirikiri text sizes");
            }
            std::vector<std::uint8_t> decoded(static_cast<std::size_t>(original_size));
            uLongf decoded_size = static_cast<uLongf>(decoded.size());
            const auto result = uncompress(decoded.data(), &decoded_size, bytes.data() + 21,
                                           static_cast<uLong>(compressed_size));
            if (result != Z_OK || decoded_size != decoded.size()) {
                throw std::runtime_error("cannot decompress Kirikiri text");
            }
            utf8 = utf16le(decoded, -1);
            return true;
        }
        // Kirikiri also accepts legacy multibyte text. UTF-8 is the safe
        // portable default; Shift-JIS conversion is supplied by the future
        // full storage adapter where encoding selection is available.
        utf8.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

} // namespace krkrvita
