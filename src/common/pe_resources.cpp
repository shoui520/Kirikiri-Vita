#include "krkrvita/pe_resources.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace krkrvita {
namespace {

template <typename T>
std::optional<T> read_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return std::nullopt;
    }
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<Unsigned>(bytes[offset + i]) << (i * 8);
    }
    return static_cast<T>(value);
}

std::string utf16le_to_utf8(const std::vector<std::uint8_t>& bytes,
                            std::size_t offset, std::size_t units) {
    std::string output;
    for (std::size_t i = 0; i < units; ++i) {
        const auto first = read_le<std::uint16_t>(bytes, offset + i * 2);
        if (!first || *first == 0) {
            break;
        }
        std::uint32_t cp = *first;
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < units) {
            const auto second = read_le<std::uint16_t>(bytes, offset + (i + 1) * 2);
            if (second && *second >= 0xdc00 && *second <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (*second - 0xdc00);
                ++i;
            }
        }
        if (cp <= 0x7f) {
            output.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }
    return output;
}

std::string version_value(const std::vector<std::uint8_t>& resource,
                          std::string_view ascii_key) {
    std::vector<std::uint8_t> needle;
    for (const char c : ascii_key) {
        needle.push_back(static_cast<std::uint8_t>(c));
        needle.push_back(0);
    }
    const auto match = std::search(resource.begin(), resource.end(),
                                   needle.begin(), needle.end());
    if (match == resource.end()) {
        return {};
    }
    const auto key_offset = static_cast<std::size_t>(match - resource.begin());
    if (key_offset < 6) {
        return {};
    }
    const auto value_length = read_le<std::uint16_t>(resource, key_offset - 4);
    if (!value_length || *value_length == 0) {
        return {};
    }
    auto value_offset = key_offset + (ascii_key.size() + 1) * 2;
    value_offset = (value_offset + 3) & ~std::size_t(3);
    if (value_offset >= resource.size()) {
        return {};
    }
    const auto available = (resource.size() - value_offset) / 2;
    return utf16le_to_utf8(resource, value_offset,
                           std::min<std::size_t>(*value_length, available));
}

} // namespace

PeResources::PeResources(const std::filesystem::path& executable) {
    std::ifstream stream(executable, std::ios::binary);
    if (!stream) {
        error_ = "cannot open executable";
        return;
    }
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length <= 0 || length > std::numeric_limits<std::uint32_t>::max()) {
        error_ = "invalid executable length";
        return;
    }
    image_.resize(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(image_.data()), length);

    if (image_.size() < 0x40 || image_[0] != 'M' || image_[1] != 'Z') {
        error_ = "missing MZ header";
        return;
    }
    const auto pe_offset = read_le<std::uint32_t>(image_, 0x3c);
    if (!pe_offset || *pe_offset > image_.size() - 24 ||
        std::memcmp(image_.data() + *pe_offset, "PE\0\0", 4) != 0) {
        error_ = "missing PE header";
        return;
    }
    const auto section_count = read_le<std::uint16_t>(image_, *pe_offset + 6);
    const auto optional_size = read_le<std::uint16_t>(image_, *pe_offset + 20);
    if (!section_count || !optional_size) {
        error_ = "truncated COFF header";
        return;
    }
    const auto optional = static_cast<std::size_t>(*pe_offset) + 24;
    const auto magic = read_le<std::uint16_t>(image_, optional);
    if (!magic || (*magic != 0x10b && *magic != 0x20b)) {
        error_ = "unsupported PE optional header";
        return;
    }
    const auto directory_base = optional + (*magic == 0x10b ? 96 : 112);
    const auto resource_rva = read_le<std::uint32_t>(image_, directory_base + 16);
    if (!resource_rva || *resource_rva == 0) {
        error_ = "PE has no resources";
        return;
    }

    const auto section_table = optional + *optional_size;
    if (section_table > image_.size() ||
        static_cast<std::size_t>(*section_count) > (image_.size() - section_table) / 40) {
        error_ = "truncated PE section table";
        return;
    }
    for (std::uint16_t i = 0; i < *section_count; ++i) {
        const auto entry = section_table + i * 40;
        sections_.push_back({
            *read_le<std::uint32_t>(image_, entry + 12),
            *read_le<std::uint32_t>(image_, entry + 8),
            *read_le<std::uint32_t>(image_, entry + 20),
            *read_le<std::uint32_t>(image_, entry + 16),
        });
    }
    const auto resource_offset = rva_to_offset(*resource_rva);
    if (!resource_offset) {
        error_ = "resource RVA is unmapped";
        return;
    }
    resource_base_ = *resource_offset;
    valid_ = true;
}

std::optional<std::size_t> PeResources::rva_to_offset(std::uint32_t rva) const {
    for (const auto& section : sections_) {
        const auto span = std::max(section.virtual_size, section.raw_size);
        if (rva >= section.virtual_address && rva - section.virtual_address < span) {
            const auto offset = static_cast<std::uint64_t>(section.raw_offset) +
                                (rva - section.virtual_address);
            if (offset < image_.size()) {
                return static_cast<std::size_t>(offset);
            }
        }
    }
    return std::nullopt;
}

std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>
PeResources::resources(std::uint32_t wanted_type) const {
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> result;
    if (!valid_ || resource_base_ > image_.size() - 16) {
        return result;
    }

    const auto entries_at = [&](std::uint32_t relative) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
        const auto directory = resource_base_ + relative;
        if (directory > image_.size() - 16) {
            return entries;
        }
        const auto named = read_le<std::uint16_t>(image_, directory + 12).value_or(0);
        const auto ids = read_le<std::uint16_t>(image_, directory + 14).value_or(0);
        const auto count = static_cast<std::size_t>(named) + ids;
        if (count > (image_.size() - directory - 16) / 8) {
            return entries;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto key = *read_le<std::uint32_t>(image_, directory + 16 + i * 8);
            const auto value = *read_le<std::uint32_t>(image_, directory + 20 + i * 8);
            if ((key & 0x80000000u) == 0) {
                entries.emplace_back(key, value);
            }
        }
        return entries;
    };

    std::optional<std::uint32_t> type_directory;
    for (const auto& [type, value] : entries_at(0)) {
        if (type == wanted_type && (value & 0x80000000u)) {
            type_directory = value & 0x7fffffffu;
            break;
        }
    }
    if (!type_directory) {
        return result;
    }
    for (const auto& [name, name_value] : entries_at(*type_directory)) {
        if ((name_value & 0x80000000u) == 0) {
            continue;
        }
        const auto languages = entries_at(name_value & 0x7fffffffu);
        if (languages.empty()) {
            continue;
        }
        const auto data_value = languages.front().second;
        if (data_value & 0x80000000u) {
            continue;
        }
        const auto data_entry = resource_base_ + data_value;
        const auto rva = read_le<std::uint32_t>(image_, data_entry);
        const auto size = read_le<std::uint32_t>(image_, data_entry + 4);
        if (!rva || !size) {
            continue;
        }
        const auto offset = rva_to_offset(*rva);
        if (!offset || *offset > image_.size() || *size > image_.size() - *offset) {
            continue;
        }
        result.emplace_back(name, std::vector<std::uint8_t>(
            image_.begin() + *offset, image_.begin() + *offset + *size));
    }
    return result;
}

std::optional<std::vector<std::uint8_t>>
PeResources::resource(std::uint32_t type, std::uint32_t name) const {
    auto all = resources(type);
    const auto found = std::find_if(all.begin(), all.end(),
        [&](const auto& value) { return value.first == name; });
    if (found == all.end()) {
        return std::nullopt;
    }
    return std::move(found->second);
}

PeMetadata PeResources::metadata() const {
    PeMetadata metadata;
    const auto versions = resources(16); // RT_VERSION
    if (versions.empty()) {
        return metadata;
    }
    const auto& bytes = versions.front().second;
    metadata.product_name = version_value(bytes, "ProductName");
    metadata.file_description = version_value(bytes, "FileDescription");
    metadata.company_name = version_value(bytes, "CompanyName");
    metadata.original_filename = version_value(bytes, "OriginalFilename");
    return metadata;
}

std::optional<EmbeddedIcon> PeResources::largest_icon() const {
    const auto groups = resources(14); // RT_GROUP_ICON
    if (groups.empty()) {
        return std::nullopt;
    }

    std::optional<EmbeddedIcon> best;
    std::uint64_t best_score = 0;
    for (const auto& [unused, group] : groups) {
        (void)unused;
        const auto type = read_le<std::uint16_t>(group, 2);
        const auto count = read_le<std::uint16_t>(group, 4);
        if (!type || *type != 1 || !count ||
            static_cast<std::size_t>(*count) > (group.size() - 6) / 14) {
            continue;
        }
        for (std::uint16_t i = 0; i < *count; ++i) {
            const auto offset = 6 + i * 14;
            const auto width = group[offset] == 0 ? 256 : group[offset];
            const auto height = group[offset + 1] == 0 ? 256 : group[offset + 1];
            const auto depth = read_le<std::uint16_t>(group, offset + 6).value_or(0);
            const auto id = read_le<std::uint16_t>(group, offset + 12);
            if (!id) {
                continue;
            }
            auto bytes = resource(3, *id); // RT_ICON
            if (!bytes) {
                continue;
            }
            const auto score = static_cast<std::uint64_t>(width) * height * 128 + depth;
            if (!best || score > best_score) {
                best = EmbeddedIcon{static_cast<std::uint16_t>(width),
                                    static_cast<std::uint16_t>(height), depth,
                                    std::move(*bytes)};
                best_score = score;
            }
        }
    }
    return best;
}

} // namespace krkrvita

