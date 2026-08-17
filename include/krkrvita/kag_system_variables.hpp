#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace krkrvita {

constexpr char16_t ascii_lower(char16_t value) noexcept {
    return value >= u'A' && value <= u'Z'
        ? static_cast<char16_t>(value + (u'a' - u'A'))
        : value;
}

constexpr bool ascii_iequals(std::u16string_view lhs,
                             std::u16string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (ascii_lower(lhs[index]) != ascii_lower(rhs[index])) return false;
    }
    return true;
}

constexpr bool is_kag_system_variable_filename(
    std::u16string_view filename) noexcept {
    constexpr std::u16string_view compact_suffix = u"sc.ksd";
    constexpr std::u16string_view user_suffix = u"su.ksd";
    if (filename.size() < compact_suffix.size()) return false;
    const auto suffix = filename.substr(filename.size() - compact_suffix.size());
    return ascii_iequals(suffix, compact_suffix) ||
           ascii_iequals(suffix, user_suffix);
}

// Both KAG 3.30 and 3.32 build these two names directly below
// System.dataPath.  Requiring that exact normalized directory prevents a
// damaged scenario, patch, or ordinary save slot from being mistaken for the
// reconstructable system-variable dictionaries.
constexpr bool is_kag_system_variable_storage(
    std::u16string_view normalized_storage,
    std::u16string_view normalized_data_path) noexcept {
    if (normalized_storage.empty() || normalized_data_path.empty()) return false;

    const std::size_t separator = normalized_storage.find_last_of(u"/\\");
    if (separator == std::u16string_view::npos) return false;
    auto directory = normalized_storage.substr(0, separator + 1);
    while (!directory.empty() &&
           (directory.back() == u'/' || directory.back() == u'\\'))
        directory.remove_suffix(1);
    while (!normalized_data_path.empty() &&
           (normalized_data_path.back() == u'/' ||
            normalized_data_path.back() == u'\\'))
        normalized_data_path.remove_suffix(1);
    if (!ascii_iequals(directory, normalized_data_path)) return false;
    return is_kag_system_variable_filename(
        normalized_storage.substr(separator + 1));
}

// A small deterministic fingerprint for hardware diagnostics.  Hash both
// bytes of every UTF-16 unit explicitly so the value is host-endian neutral.
constexpr std::uint64_t fnv1a_utf16(std::u16string_view text) noexcept {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (char16_t unit : text) {
        hash ^= static_cast<std::uint8_t>(unit & 0xffu);
        hash *= UINT64_C(1099511628211);
        hash ^= static_cast<std::uint8_t>((unit >> 8u) & 0xffu);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

inline std::string corrupt_system_variable_backup_name(
    std::string_view native_path, unsigned attempt) {
    std::string candidate(native_path);
    candidate += ".krkrvita-corrupt";
    if (attempt != 0) {
        candidate.push_back('.');
        candidate += std::to_string(attempt);
    }
    return candidate;
}

template <typename Exists, typename Rename>
std::string quarantine_corrupt_system_variable(
    std::string_view native_path, Exists&& exists, Rename&& rename,
    unsigned maximum_attempts = 100) {
    for (unsigned attempt = 0; attempt < maximum_attempts; ++attempt) {
        std::string candidate =
            corrupt_system_variable_backup_name(native_path, attempt);
        if (exists(candidate)) continue;
        if (rename(native_path, candidate)) return candidate;
        return {};
    }
    return {};
}

} // namespace krkrvita
