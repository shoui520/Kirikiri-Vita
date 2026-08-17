#include "krkrvita/psb.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace krkrvita {
namespace {

constexpr std::size_t kMaxPsbBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxPsbItems = 4u * 1024u * 1024u;
constexpr unsigned kMaxPsbDepth = 256;

void set_error(std::string* output, const std::string& message) {
    if (output) *output = message;
}

bool add_size(std::size_t left, std::size_t right, std::size_t& result) {
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

class Parser {
public:
    Parser(const std::uint8_t* bytes, std::size_t size, std::string* error)
        : bytes_(bytes), size_(size), error_(error) {}

    bool parse(PsbDocument& document) {
        if (!bytes_ || size_ < 40 || size_ > kMaxPsbBytes ||
            std::memcmp(bytes_, "PSB\0", 4) != 0) {
            return fail("Not a supported PSB file");
        }
        if (!read_u16(4, version_) || !read_u16(6, encryption_) ||
            version_ < 2 || version_ > 4 || encryption_ != 0) {
            return fail("Unsupported or encrypted PSB header");
        }
        if (!read_u32(12, names_offset_) ||
            !read_u32(16, strings_offset_) ||
            !read_u32(20, string_data_offset_) ||
            !read_u32(24, chunk_offsets_offset_) ||
            !read_u32(28, chunk_lengths_offset_) ||
            !read_u32(32, chunk_data_offset_) ||
            !read_u32(36, entries_offset_)) {
            return fail("Truncated PSB header");
        }
        if (version_ >= 4) {
            if (size_ < 56 || !read_u32(44, extra_offsets_offset_) ||
                !read_u32(48, extra_lengths_offset_) ||
                !read_u32(52, extra_data_offset_)) {
                return fail("Truncated PSB v4 header");
            }
        }
        // Tables are read starting with a type byte, so their offset must
        // address a real byte.
        const std::uint32_t table_offsets[] = {
            names_offset_, strings_offset_, chunk_offsets_offset_,
            chunk_lengths_offset_, entries_offset_};
        for (const auto offset : table_offsets) {
            if (offset >= size_) return fail("PSB table offset is out of range");
        }
        // The string and resource payload regions are bases that entries index
        // into, not tables. An empty region legitimately begins one past the
        // last byte, which is how every Noble Works scene state is encoded:
        // no embedded resources, so offsetChunkData == the document size.
        // Individual reads are still bounds-checked against the payload.
        const std::uint32_t data_offsets[] = {
            string_data_offset_, chunk_data_offset_};
        for (const auto offset : data_offsets) {
            if (offset > size_) return fail("PSB data offset is out of range");
        }
        std::size_t cursor = names_offset_;
        if (!read_array(cursor, charset_) || !read_array(cursor, names_data_) ||
            !read_array(cursor, name_indexes_) || !decode_names()) {
            return false;
        }
        cursor = strings_offset_;
        if (!read_array(cursor, string_offsets_)) return false;
        cursor = chunk_offsets_offset_;
        if (!read_array(cursor, chunk_offsets_)) return false;
        cursor = chunk_lengths_offset_;
        if (!read_array(cursor, chunk_lengths_) ||
            chunk_offsets_.size() != chunk_lengths_.size()) {
            return fail("PSB resource tables disagree");
        }
        resources_.resize(chunk_offsets_.size());
        if (version_ >= 4) {
            if (extra_offsets_offset_ >= size_ ||
                extra_lengths_offset_ >= size_) {
                return fail("PSB extra resource offset is out of range");
            }
            // Same rule as chunk data: an empty extra payload starts at the end.
            if (extra_data_offset_ > size_) {
                return fail("PSB extra resource offset is out of range");
            }
            cursor = extra_offsets_offset_;
            if (!read_array(cursor, extra_offsets_)) return false;
            cursor = extra_lengths_offset_;
            if (!read_array(cursor, extra_lengths_) ||
                extra_offsets_.size() != extra_lengths_.size()) {
                return fail("PSB extra resource tables disagree");
            }
            extra_resources_.resize(extra_offsets_.size());
        }
        cursor = entries_offset_;
        document.root = read_value(cursor, 0);
        if (!document.root) return false;
        document.version = version_;
        return true;
    }

private:
    bool fail(const std::string& message) {
        set_error(error_, message);
        return false;
    }

    bool available(std::size_t offset, std::size_t amount) const {
        return offset <= size_ && amount <= size_ - offset;
    }

    bool read_u16(std::size_t offset, std::uint16_t& value) const {
        if (!available(offset, 2)) return false;
        value = static_cast<std::uint16_t>(bytes_[offset]) |
                static_cast<std::uint16_t>(bytes_[offset + 1]) << 8;
        return true;
    }

    bool read_u32(std::size_t offset, std::uint32_t& value) const {
        if (!available(offset, 4)) return false;
        value = static_cast<std::uint32_t>(bytes_[offset]) |
                static_cast<std::uint32_t>(bytes_[offset + 1]) << 8 |
                static_cast<std::uint32_t>(bytes_[offset + 2]) << 16 |
                static_cast<std::uint32_t>(bytes_[offset + 3]) << 24;
        return true;
    }

    bool read_uint(std::size_t& cursor, unsigned width, std::uint64_t& value) {
        if (width > 8 || !available(cursor, width))
            return fail("Truncated PSB integer");
        value = 0;
        for (unsigned index = 0; index < width; ++index)
            value |= static_cast<std::uint64_t>(bytes_[cursor + index]) <<
                     (index * 8);
        cursor += width;
        return true;
    }

    bool read_array(std::size_t& cursor, std::vector<std::uint32_t>& output) {
        if (!available(cursor, 1)) return fail("Truncated PSB array type");
        const auto type = bytes_[cursor++];
        if (type < 0x0d || type > 0x14)
            return fail("Invalid PSB array type");
        const unsigned count_width = type - 0x0c;
        std::uint64_t count64 = 0;
        if (!read_uint(cursor, count_width, count64) ||
            count64 > kMaxPsbItems) {
            return fail("PSB array is too large");
        }
        if (!available(cursor, 1)) return fail("Truncated PSB array width");
        const auto width_type = bytes_[cursor++];
        if (width_type < 0x0c || width_type > 0x14)
            return fail("Invalid PSB array entry width");
        const unsigned entry_width = width_type - 0x0c;
        const auto count = static_cast<std::size_t>(count64);
        if (entry_width != 0 && count > (size_ - cursor) / entry_width)
            return fail("Truncated PSB array data");
        output.clear();
        output.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            std::uint64_t value = 0;
            if (!read_uint(cursor, entry_width, value) ||
                value > std::numeric_limits<std::uint32_t>::max()) {
                return fail("PSB array value is out of range");
            }
            output.push_back(static_cast<std::uint32_t>(value));
        }
        return true;
    }

    bool decode_names() {
        names_.clear();
        names_.reserve(name_indexes_.size());
        for (const auto index : name_indexes_) {
            if (index >= names_data_.size())
                return fail("PSB name index is out of range");
            std::vector<std::uint8_t> reversed;
            std::uint32_t character = names_data_[index];
            for (std::size_t steps = 0; character != 0; ++steps) {
                if (steps >= names_data_.size() || character >= names_data_.size())
                    return fail("PSB name trie contains a cycle");
                const auto code = names_data_[character];
                if (code >= charset_.size() || charset_[code] > character)
                    return fail("PSB name trie is invalid");
                const auto byte = character - charset_[code];
                if (byte > 0xff) return fail("PSB name byte is invalid");
                reversed.push_back(static_cast<std::uint8_t>(byte));
                character = code;
            }
            std::reverse(reversed.begin(), reversed.end());
            if (reversed.empty()) {
                names_.emplace_back();
            } else {
                names_.emplace_back(
                    reinterpret_cast<const char*>(reversed.data()), reversed.size());
            }
        }
        return true;
    }

    PsbValuePtr make_value(PsbValue::Type type) {
        if (++items_ > kMaxPsbItems) {
            fail("PSB object graph is too large");
            return {};
        }
        auto value = std::make_shared<PsbValue>();
        value->type = type;
        return value;
    }

    static std::int64_t signed_integer(std::uint64_t bits, unsigned width) {
        if (width == 4 && bits > 0x7fffffffULL)
            return -1 - static_cast<std::int64_t>((~bits) & 0xffffffffULL);
        if (width == 8 && bits > 0x7fffffffffffffffULL)
            return -1 - static_cast<std::int64_t>(~bits);
        return static_cast<std::int64_t>(bits);
    }

    bool read_c_string(std::size_t offset, std::string& output) {
        if (offset >= size_) return fail("PSB string offset is out of range");
        const auto* begin = bytes_ + offset;
        const auto* end = static_cast<const std::uint8_t*>(
            std::memchr(begin, 0, size_ - offset));
        if (!end) return fail("Unterminated PSB string");
        output.assign(reinterpret_cast<const char*>(begin),
                      static_cast<std::size_t>(end - begin));
        return true;
    }

    std::shared_ptr<const std::vector<std::uint8_t>> resource(
        std::uint32_t index, bool extra) {
        auto& offsets = extra ? extra_offsets_ : chunk_offsets_;
        auto& lengths = extra ? extra_lengths_ : chunk_lengths_;
        auto& cache = extra ? extra_resources_ : resources_;
        const std::size_t base = extra ? extra_data_offset_ : chunk_data_offset_;
        if (index >= offsets.size()) {
            fail("PSB resource index is out of range");
            return {};
        }
        if (cache[index]) return cache[index];
        std::size_t position = 0;
        if (!add_size(base, offsets[index], position) ||
            !available(position, lengths[index])) {
            fail("PSB resource bytes are out of range");
            return {};
        }
        cache[index] = std::make_shared<const std::vector<std::uint8_t>>(
            bytes_ + position, bytes_ + position + lengths[index]);
        return cache[index];
    }

    PsbValuePtr read_value(std::size_t& cursor, unsigned depth) {
        if (depth > kMaxPsbDepth || !available(cursor, 1)) {
            fail(depth > kMaxPsbDepth ? "PSB object nesting is too deep"
                                     : "Truncated PSB value");
            return {};
        }
        const auto type = bytes_[cursor++];
        if (type == 0x01) return make_value(PsbValue::Type::null_value);
        if (type == 0x02 || type == 0x03) {
            auto value = make_value(PsbValue::Type::boolean);
            if (value) value->boolean = type == 0x03;
            return value;
        }
        if (type >= 0x04 && type <= 0x0c) {
            const unsigned width = type - 0x04;
            std::uint64_t bits = 0;
            if (!read_uint(cursor, width, bits)) return {};
            auto value = make_value(PsbValue::Type::integer);
            if (value) value->integer = signed_integer(bits, width);
            return value;
        }
        if (type >= 0x0d && type <= 0x14) {
            --cursor;
            std::vector<std::uint32_t> array;
            if (!read_array(cursor, array)) return {};
            auto value = make_value(PsbValue::Type::array);
            if (!value) return {};
            value->array.reserve(array.size());
            for (const auto item : array) {
                auto child = make_value(PsbValue::Type::integer);
                if (!child) return {};
                child->integer = item;
                value->array.push_back(std::move(child));
            }
            return value;
        }
        if (type >= 0x15 && type <= 0x18) {
            std::uint64_t index = 0;
            if (!read_uint(cursor, type - 0x14, index) ||
                index >= string_offsets_.size()) {
                fail("PSB string index is out of range");
                return {};
            }
            std::size_t position = 0;
            if (!add_size(string_data_offset_, string_offsets_[index], position)) {
                fail("PSB string offset overflow");
                return {};
            }
            auto value = make_value(PsbValue::Type::string);
            if (!value || !read_c_string(position, value->string)) return {};
            return value;
        }
        if ((type >= 0x19 && type <= 0x1c) ||
            (type >= 0x22 && type <= 0x25)) {
            const bool extra = type >= 0x22;
            std::uint64_t index = 0;
            const unsigned width = type - (extra ? 0x21 : 0x18);
            if (!read_uint(cursor, width, index) ||
                index > std::numeric_limits<std::uint32_t>::max()) {
                fail("PSB resource index is invalid");
                return {};
            }
            auto value = make_value(PsbValue::Type::binary);
            if (!value) return {};
            value->binary = resource(static_cast<std::uint32_t>(index), extra);
            return value->binary ? value : PsbValuePtr{};
        }
        if (type == 0x1d) {
            auto value = make_value(PsbValue::Type::real);
            return value;
        }
        if (type == 0x1e || type == 0x1f) {
            const unsigned width = type == 0x1e ? 4 : 8;
            std::uint64_t bits = 0;
            if (!read_uint(cursor, width, bits)) return {};
            auto value = make_value(PsbValue::Type::real);
            if (!value) return {};
            if (width == 4) {
                const auto word = static_cast<std::uint32_t>(bits);
                float number = 0;
                std::memcpy(&number, &word, sizeof(number));
                value->real = number;
            } else {
                double number = 0;
                std::memcpy(&number, &bits, sizeof(number));
                value->real = number;
            }
            return value;
        }
        if (type == 0x20) {
            std::vector<std::uint32_t> offsets;
            if (!read_array(cursor, offsets)) return {};
            const auto base = cursor;
            auto value = make_value(PsbValue::Type::array);
            if (!value) return {};
            value->array.reserve(offsets.size());
            for (const auto offset : offsets) {
                std::size_t child_cursor = 0;
                if (!add_size(base, offset, child_cursor) || child_cursor >= size_) {
                    fail("PSB list offset is out of range");
                    return {};
                }
                auto child = read_value(child_cursor, depth + 1);
                if (!child) return {};
                value->array.push_back(std::move(child));
            }
            return value;
        }
        if (type == 0x21) {
            std::vector<std::uint32_t> name_indexes;
            std::vector<std::uint32_t> offsets;
            if (!read_array(cursor, name_indexes) ||
                !read_array(cursor, offsets) ||
                name_indexes.size() != offsets.size()) {
                fail("PSB object tables disagree");
                return {};
            }
            const auto base = cursor;
            auto value = make_value(PsbValue::Type::object);
            if (!value) return {};
            value->object.reserve(offsets.size());
            for (std::size_t index = 0; index < offsets.size(); ++index) {
                if (name_indexes[index] >= names_.size()) {
                    fail("PSB object name is out of range");
                    return {};
                }
                std::size_t child_cursor = 0;
                if (!add_size(base, offsets[index], child_cursor) ||
                    child_cursor >= size_) {
                    fail("PSB object offset is out of range");
                    return {};
                }
                auto child = read_value(child_cursor, depth + 1);
                if (!child) return {};
                value->object.emplace_back(names_[name_indexes[index]],
                                           std::move(child));
            }
            return value;
        }
        fail("Unknown PSB value type");
        return {};
    }

    const std::uint8_t* bytes_ = nullptr;
    std::size_t size_ = 0;
    std::string* error_ = nullptr;
    std::uint16_t version_ = 0;
    std::uint16_t encryption_ = 0;
    std::uint32_t names_offset_ = 0;
    std::uint32_t strings_offset_ = 0;
    std::uint32_t string_data_offset_ = 0;
    std::uint32_t chunk_offsets_offset_ = 0;
    std::uint32_t chunk_lengths_offset_ = 0;
    std::uint32_t chunk_data_offset_ = 0;
    std::uint32_t entries_offset_ = 0;
    std::uint32_t extra_offsets_offset_ = 0;
    std::uint32_t extra_lengths_offset_ = 0;
    std::uint32_t extra_data_offset_ = 0;
    std::vector<std::uint32_t> charset_;
    std::vector<std::uint32_t> names_data_;
    std::vector<std::uint32_t> name_indexes_;
    std::vector<std::string> names_;
    std::vector<std::uint32_t> string_offsets_;
    std::vector<std::uint32_t> chunk_offsets_;
    std::vector<std::uint32_t> chunk_lengths_;
    std::vector<std::uint32_t> extra_offsets_;
    std::vector<std::uint32_t> extra_lengths_;
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> resources_;
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> extra_resources_;
    std::size_t items_ = 0;
};

} // namespace

bool parse_psb(const std::uint8_t* bytes, std::size_t size,
               PsbDocument& document, std::string* error) {
    document = {};
    if (!bytes || size < 4 || size > kMaxPsbBytes) {
        set_error(error, "Invalid PSB input size");
        return false;
    }
    if (size >= 8 && std::memcmp(bytes, "mdf\0", 4) == 0) {
        const std::uint32_t output_size =
            static_cast<std::uint32_t>(bytes[4]) |
            static_cast<std::uint32_t>(bytes[5]) << 8 |
            static_cast<std::uint32_t>(bytes[6]) << 16 |
            static_cast<std::uint32_t>(bytes[7]) << 24;
        if (output_size < 40 || output_size > kMaxPsbBytes) {
            set_error(error, "Invalid MDF decompressed size");
            return false;
        }
        std::vector<std::uint8_t> unpacked(output_size);
        uLongf actual = output_size;
        const int result = uncompress(unpacked.data(), &actual, bytes + 8,
                                      static_cast<uLong>(size - 8));
        if (result != Z_OK || actual != output_size) {
            set_error(error, "Cannot decompress MDF PSB payload");
            return false;
        }
        Parser parser(unpacked.data(), unpacked.size(), error);
        if (!parser.parse(document)) return false;
        document.mdf_compressed = true;
        return true;
    }
    Parser parser(bytes, size, error);
    return parser.parse(document);
}

} // namespace krkrvita
