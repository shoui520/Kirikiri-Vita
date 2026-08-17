#include "krkrvita/native_plugin_inventory.hpp"

#include "krkrvita/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

namespace krkrvita {
namespace {

template <typename T>
std::optional<T> read_le(std::span<const std::uint8_t> bytes,
                         std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        return std::nullopt;
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index)
        value |= static_cast<Unsigned>(bytes[offset + index]) << (index * 8);
    return static_cast<T>(value);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string machine_name(std::uint16_t machine) {
    switch (machine) {
    case 0x014c: return "x86";
    case 0x8664: return "x86_64";
    case 0x01c0: return "arm";
    case 0x01c4: return "armv7";
    case 0xaa64: return "arm64";
    default: return "0x" + [] (std::uint16_t value) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string text(4, '0');
        for (int index = 3; index >= 0; --index) {
            text[static_cast<std::size_t>(index)] = digits[value & 15u];
            value >>= 4;
        }
        return text;
    }(machine);
    }
}

struct PeSection {
    std::uint32_t virtual_address = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_offset = 0;
    std::uint32_t raw_size = 0;
};

std::optional<std::size_t> rva_to_offset(
    std::span<const std::uint8_t> bytes,
    std::span<const PeSection> sections,
    std::uint32_t size_of_headers,
    std::uint32_t rva) {
    if (rva < size_of_headers && rva < bytes.size())
        return static_cast<std::size_t>(rva);
    for (const auto& section : sections) {
        const auto span = std::max(section.virtual_size, section.raw_size);
        if (rva < section.virtual_address ||
            rva - section.virtual_address >= span) continue;
        const auto relative = rva - section.virtual_address;
        if (relative >= section.raw_size) return std::nullopt;
        const auto offset = static_cast<std::uint64_t>(section.raw_offset) +
                            relative;
        if (offset >= bytes.size()) return std::nullopt;
        return static_cast<std::size_t>(offset);
    }
    return std::nullopt;
}

std::optional<std::string> read_ascii_z(
    std::span<const std::uint8_t> bytes, std::size_t offset,
    std::size_t maximum = 1024) {
    if (offset >= bytes.size()) return std::nullopt;
    std::string result;
    const auto limit = std::min(bytes.size(), offset + maximum);
    for (; offset < limit; ++offset) {
        const auto byte = bytes[offset];
        if (byte == 0) return result;
        if (byte < 0x20 || byte > 0x7e) return std::nullopt;
        result.push_back(static_cast<char>(byte));
    }
    return std::nullopt;
}

void parse_pe(NativePluginBinary& plugin,
              std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') {
        plugin.error = "missing MZ header";
        return;
    }
    const auto pe_offset = read_le<std::uint32_t>(bytes, 0x3c);
    if (!pe_offset || *pe_offset > bytes.size() - 24 ||
        std::memcmp(bytes.data() + *pe_offset, "PE\0\0", 4) != 0) {
        plugin.error = "missing PE header";
        return;
    }
    const auto machine = read_le<std::uint16_t>(bytes, *pe_offset + 4);
    const auto section_count = read_le<std::uint16_t>(bytes, *pe_offset + 6);
    const auto optional_size = read_le<std::uint16_t>(bytes, *pe_offset + 20);
    if (!machine || !section_count || !optional_size) {
        plugin.error = "truncated COFF header";
        return;
    }
    plugin.machine = machine_name(*machine);
    const auto optional = static_cast<std::size_t>(*pe_offset) + 24;
    const auto magic = read_le<std::uint16_t>(bytes, optional);
    if (!magic || (*magic != 0x10b && *magic != 0x20b)) {
        plugin.error = "unsupported PE optional header";
        return;
    }
    const auto directory_base = optional + (*magic == 0x10b ? 96u : 112u);
    const auto directory_count_offset = optional +
        (*magic == 0x10b ? 92u : 108u);
    const auto directory_count = read_le<std::uint32_t>(
        bytes, directory_count_offset);
    const auto size_of_headers = read_le<std::uint32_t>(bytes, optional + 60);
    if (!directory_count || !size_of_headers || directory_base > bytes.size()) {
        plugin.error = "truncated PE data directories";
        return;
    }
    const auto section_table = optional + *optional_size;
    if (section_table > bytes.size() ||
        static_cast<std::size_t>(*section_count) >
            (bytes.size() - section_table) / 40) {
        plugin.error = "truncated PE section table";
        return;
    }
    std::vector<PeSection> sections;
    sections.reserve(*section_count);
    for (std::uint16_t index = 0; index < *section_count; ++index) {
        const auto entry = section_table + index * 40u;
        sections.push_back(PeSection{
            read_le<std::uint32_t>(bytes, entry + 12).value_or(0),
            read_le<std::uint32_t>(bytes, entry + 8).value_or(0),
            read_le<std::uint32_t>(bytes, entry + 20).value_or(0),
            read_le<std::uint32_t>(bytes, entry + 16).value_or(0)});
    }
    plugin.valid_pe = true;
    if (*directory_count <= 1 || directory_base > bytes.size() - 16)
        return;
    const auto import_rva = read_le<std::uint32_t>(bytes, directory_base + 8);
    const auto import_size = read_le<std::uint32_t>(bytes, directory_base + 12);
    if (!import_rva || !import_size || *import_rva == 0 || *import_size < 20)
        return;
    const auto import_offset = rva_to_offset(bytes, sections,
                                              *size_of_headers, *import_rva);
    if (!import_offset) {
        plugin.error = "unmapped PE import directory";
        return;
    }
    const auto descriptor_count = std::min<std::size_t>(*import_size / 20u,
                                                         4096u);
    for (std::size_t index = 0; index < descriptor_count; ++index) {
        const auto descriptor = *import_offset + index * 20u;
        if (descriptor > bytes.size() || 20u > bytes.size() - descriptor) {
            plugin.error = "truncated PE import directory";
            break;
        }
        bool empty = true;
        for (std::size_t byte = 0; byte < 20; ++byte)
            empty = empty && bytes[descriptor + byte] == 0;
        if (empty) break;
        const auto name_rva = read_le<std::uint32_t>(bytes, descriptor + 12);
        if (!name_rva) continue;
        const auto name_offset = rva_to_offset(bytes, sections,
                                                *size_of_headers, *name_rva);
        if (!name_offset) continue;
        if (const auto name = read_ascii_z(bytes, *name_offset))
            plugin.imports.push_back(lower_ascii(*name));
    }
    std::sort(plugin.imports.begin(), plugin.imports.end());
    plugin.imports.erase(std::unique(plugin.imports.begin(),
                                     plugin.imports.end()),
                         plugin.imports.end());
}

NativePluginBinary inspect_plugin(const std::filesystem::path& root,
                                  const std::filesystem::path& path) {
    NativePluginBinary plugin;
    plugin.path = path;
    std::error_code error;
    plugin.relative_path = std::filesystem::relative(path, root, error);
    if (error) plugin.relative_path = path.filename();
    plugin.module = lower_ascii(path.filename().string());
    plugin.size = std::filesystem::file_size(path, error);
    if (error) {
        plugin.error = "cannot determine plugin size: " + error.message();
        return plugin;
    }
    if (plugin.size > 256u * 1024u * 1024u) {
        plugin.error = "plugin exceeds 256 MiB inspection bound";
        return plugin;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        plugin.error = "cannot open plugin";
        return plugin;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(plugin.size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            plugin.error = "cannot read complete plugin";
            return plugin;
        }
    }
    Sha256 digest;
    digest.update(bytes.data(), bytes.size());
    plugin.sha256 = Sha256::hex(digest.finish());
    parse_pe(plugin, bytes);
    return plugin;
}

} // namespace

std::vector<NativePluginBinary> inventory_native_plugins(
    const std::filesystem::path& game_root,
    std::size_t maximum_depth,
    std::size_t maximum_entries) {
    std::vector<NativePluginBinary> plugins;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        game_root, std::filesystem::directory_options::skip_permission_denied,
        error), end;
    if (error) throw std::runtime_error("cannot enumerate plugin tree: " +
                                        error.message());
    std::size_t entries = 0;
    while (iterator != end) {
        if (++entries > maximum_entries)
            throw std::runtime_error("plugin inventory entry bound exceeded");
        if (iterator.depth() >= static_cast<int>(maximum_depth) &&
            iterator->is_directory(error)) {
            iterator.disable_recursion_pending();
        }
        if (error) throw std::runtime_error("cannot inspect plugin path: " +
                                            error.message());
        if (iterator->is_regular_file(error)) {
            if (error) throw std::runtime_error("cannot inspect plugin type: " +
                                                error.message());
            const auto extension = lower_ascii(
                iterator->path().extension().string());
            if (extension == ".dll" || extension == ".tpm")
                plugins.push_back(inspect_plugin(game_root, iterator->path()));
        }
        iterator.increment(error);
        if (error) throw std::runtime_error("cannot enumerate plugin tree: " +
                                            error.message());
    }
    std::sort(plugins.begin(), plugins.end(), [](const auto& left,
                                                  const auto& right) {
        return left.relative_path.generic_string() <
               right.relative_path.generic_string();
    });
    return plugins;
}

} // namespace krkrvita
