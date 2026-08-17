#pragma once

#include "krkrvita/game.hpp"
#include "krkrvita/vita_input_mapping.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace krkrvita {

struct InputProfile {
    int mapping_version = vita_input_mapping_version;
    std::map<std::string, std::string> bindings;
    float analog_deadzone = 0.18f;
    float cursor_speed = 780.0f;
    bool touch_enabled = true;
};

struct GameProfile {
    std::string game_id;
    std::filesystem::path game_path;
    std::string display_name;
    std::string patch_title;
    std::string patch_brand;
    std::string patch_commit;
    std::filesystem::path patch_root;
    std::filesystem::path xp3_filter_path;
    std::string filter_origin;
    InputProfile input;

    static GameProfile defaults(const GameDescriptor& game);
    static std::optional<GameProfile> load(const std::filesystem::path& path,
                                           std::string* error = nullptr);
    bool save(const std::filesystem::path& path, std::string* error = nullptr) const;
};

} // namespace krkrvita
