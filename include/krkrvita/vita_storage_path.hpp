#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace krkrvita {

inline bool vita_path_starts_with(std::u16string_view value,
                                  std::u16string_view prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

// Vita device paths are not POSIX absolute paths. In particular, inserting a
// slash after the device colon changes the path passed to SceIofilemgr:
//
//     ux0:data/game           (native Vita path)
//     file://./ux0:data/game  (Kirikiri storage URI)
//
// This dependency-free contract is shared verbatim by Yuri and host tests.
inline std::size_t vita_device_prefix_length(std::u16string_view path) {
    const std::size_t colon = path.find(u':');
    if (colon == std::u16string_view::npos || colon == 0 || colon > 15)
        return 0;
    const std::size_t slash = path.find_first_of(u"/\\");
    if (slash != std::u16string_view::npos && slash < colon) return 0;
    // Every Vita mount exposed to applications ends in its unit number
    // (ux0:, app0:, savedata0:, uma0:, ...). This also keeps URI schemes such
    // as file: from being misclassified as devices.
    if (path[colon - 1] < u'0' || path[colon - 1] > u'9') return 0;
    for (std::size_t index = 0; index < colon; ++index) {
        const char16_t ch = path[index];
        const bool valid = (ch >= u'a' && ch <= u'z') ||
                           (ch >= u'A' && ch <= u'Z') ||
                           (ch >= u'0' && ch <= u'9') || ch == u'_';
        if (!valid) return 0;
    }
    return colon + 1;
}

inline std::u16string vita_canonical_native_path(std::u16string_view input) {
    std::u16string path(input);
    for (char16_t& ch : path)
        if (ch == u'\\') ch = u'/';

    const std::size_t prefix = vita_device_prefix_length(path);
    if (prefix) {
        std::size_t content = prefix;
        while (content < path.size() && path[content] == u'/') ++content;
        if (content != prefix) path.erase(prefix, content - prefix);
    }
    return path;
}

inline std::u16string vita_native_to_storage_path(std::u16string_view input) {
    if (vita_path_starts_with(input, u"file://")) return std::u16string(input);
    std::u16string path = vita_canonical_native_path(input);
    if (!vita_device_prefix_length(path)) return path;
    return std::u16string(u"file://./") + path;
}

// tTVPFileMedia normally receives only the domain/path part ("./ux0:data").
// Accept a full URI too so every path boundary has the same semantics.
inline std::u16string vita_storage_to_native_path(std::u16string_view input) {
    constexpr std::u16string_view uri_prefix = u"file://./";
    if (vita_path_starts_with(input, uri_prefix)) input.remove_prefix(uri_prefix.size());
    else if (vita_path_starts_with(input, u"./")) input.remove_prefix(2);
    return vita_canonical_native_path(input);
}

inline std::u16string vita_directory_path(std::u16string_view input) {
    std::u16string path = vita_canonical_native_path(input);
    if (!path.empty() && path.back() != u'/') path.push_back(u'/');
    return path;
}

} // namespace krkrvita
