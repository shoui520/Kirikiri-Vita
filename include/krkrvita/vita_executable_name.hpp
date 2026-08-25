#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

// Kirikiri's System.exeName is the full path of the game's Windows
// executable, and some titles derive sibling resources from it, for example:
//
//   var file = Storages.chopStorageExt(System.exeName) + ".cf";
//   if (Storages.isExistentStorage(file) != true) { inform(...); System.exit(); }
//
// so a wrong exeName is a hard refusal to boot, not a degraded feature.
//
// Yuri returns the project *directory* from ExePath(), because on Android there
// is no game executable. On Vita the Windows executable is still sitting in the
// staged game folder, so the faithful answer is available: find it.

inline bool vita_name_has_suffix(std::u16string_view name,
                                 std::u16string_view suffix) {
    if (name.size() < suffix.size()) return false;
    const auto offset = name.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        char16_t left = name[offset + index];
        char16_t right = suffix[index];
        if (left >= u'A' && left <= u'Z') left = static_cast<char16_t>(left + 32);
        if (right >= u'A' && right <= u'Z')
            right = static_cast<char16_t>(right + 32);
        if (left != right) return false;
    }
    return true;
}

inline std::u16string vita_name_without_extension(std::u16string_view name) {
    const auto dot = name.rfind(u'.');
    if (dot == std::u16string_view::npos) return std::u16string(name);
    return std::u16string(name.substr(0, dot));
}

// Picks the game executable from the names in a project directory, or returns
// an empty string when there is no unambiguous answer.
//
// Deliberately conservative: guessing wrong would change System.exeName for
// titles that already boot, so anything unclear keeps Yuri's directory
// behaviour instead.
inline std::u16string vita_select_executable_name(
    const std::vector<std::u16string>& names) {
    std::vector<std::u16string> executables;
    for (const auto& name : names) {
        if (vita_name_has_suffix(name, u".exe")) executables.push_back(name);
    }
    if (executables.empty()) return {};

    // A sibling <base>.cf is Kirikiri's own krkrconf pairing, so it identifies
    // the engine executable even when the folder also ships tools.
    std::u16string configured;
    for (const auto& executable : executables) {
        const std::u16string config =
            vita_name_without_extension(executable) + u".cf";
        for (const auto& name : names) {
            if (name.size() != config.size()) continue;
            if (!vita_name_has_suffix(name, config)) continue;
            if (configured.empty() || executable < configured)
                configured = executable;
            break;
        }
    }
    if (!configured.empty()) return configured;

    // Otherwise only a single candidate is safe to assume.
    if (executables.size() == 1) return executables.front();
    return {};
}

} // namespace krkrvita
