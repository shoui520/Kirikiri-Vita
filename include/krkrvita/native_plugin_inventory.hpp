#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace krkrvita {

// Static facts about a loose Windows plugin shipped with a game.  Kirikiri
// plugins are normally x86 PE DLLs even when their extension is .tpm.
struct NativePluginBinary {
    std::filesystem::path path;
    std::filesystem::path relative_path;
    std::string module;
    std::string sha256;
    std::string machine;
    std::uint64_t size = 0;
    bool valid_pe = false;
    std::vector<std::string> imports;
    std::string error;
};

// Plugin discovery is deliberately bounded.  It covers the executable
// directory and conventional nested plugin directories without walking an
// arbitrary filesystem tree.
std::vector<NativePluginBinary> inventory_native_plugins(
    const std::filesystem::path& game_root,
    std::size_t maximum_depth = 6,
    std::size_t maximum_entries = 100000);

} // namespace krkrvita
