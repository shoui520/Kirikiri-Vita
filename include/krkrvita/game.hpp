#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

struct PeMetadata {
    std::string product_name;
    std::string file_description;
    std::string company_name;
    std::string original_filename;
};

struct GameFile {
    std::filesystem::path path;
    std::string name;
    std::uint64_t size = 0;
};

struct GameDescriptor {
    std::filesystem::path root;
    std::string directory_name;
    std::filesystem::path executable;
    std::string executable_stem;
    std::string display_name;
    std::string fingerprint;
    PeMetadata pe;
    std::vector<GameFile> archives;
    std::vector<GameFile> plugins;
};

enum class GameScanMode {
    Full,
    ArchivesOnly,
};

class GameScanner {
public:
    static GameDescriptor scan(const std::filesystem::path& root,
                               GameScanMode mode = GameScanMode::Full);
};

std::string normalize_game_name(std::string_view utf8);

} // namespace krkrvita
