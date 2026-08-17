#include "krkrvita/profile.hpp"

#include "krkrvita/game.hpp"
#include "krkrvita/vita_input_mapping.hpp"

#include <fstream>
#include <sstream>

namespace krkrvita {
namespace {

std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool parse_bool(std::string_view value) {
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

} // namespace

GameProfile GameProfile::defaults(const GameDescriptor& game) {
    GameProfile profile;
    profile.game_id = game.fingerprint.substr(0, 16);
    profile.game_path = game.root;
    profile.display_name = game.display_name;
    profile.input.mapping_version = vita_input_mapping_version;
    profile.input.bindings = vita_default_input_bindings();
    return profile;
}

std::optional<GameProfile> GameProfile::load(const std::filesystem::path& path,
                                             std::string* error) {
    std::ifstream stream(path);
    if (!stream) {
        if (error) *error = "cannot open profile";
        return std::nullopt;
    }
    GameProfile profile;
    // A profile without a version predates versioned controller mappings.
    // Keep it identifiable as version 1 so the Vita runtime can migrate its
    // serialized defaults instead of treating them as current overrides.
    profile.input.mapping_version = 1;
    std::string line;
    std::size_t line_number = 0;
    try {
        while (std::getline(stream, line)) {
            ++line_number;
            line = trim(line);
            if (line.empty() || line.front() == '#' || line.front() == ';') continue;
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const auto key = trim(line.substr(0, equals));
            const auto value = trim(line.substr(equals + 1));
            if (key == "game_id") profile.game_id = value;
            else if (key == "game_path") profile.game_path = value;
            else if (key == "display_name") profile.display_name = value;
            else if (key == "patch_title") profile.patch_title = value;
            else if (key == "patch_brand") profile.patch_brand = value;
            else if (key == "patch_commit") profile.patch_commit = value;
            else if (key == "patch_root") profile.patch_root = value;
            else if (key == "xp3_filter_path") profile.xp3_filter_path = value;
            else if (key == "filter_origin") profile.filter_origin = value;
            else if (key == "input_mapping_version") profile.input.mapping_version = std::stoi(value);
            else if (key == "analog_deadzone") profile.input.analog_deadzone = std::stof(value);
            else if (key == "cursor_speed") profile.input.cursor_speed = std::stof(value);
            else if (key == "touch_enabled") profile.input.touch_enabled = parse_bool(value);
            else if (key.rfind("bind.", 0) == 0) profile.input.bindings[key.substr(5)] = value;
        }
    } catch (const std::exception& exception) {
        if (error) *error = "invalid profile line " + std::to_string(line_number) +
                            ": " + exception.what();
        return std::nullopt;
    }
    if (profile.game_id.empty() || profile.game_path.empty()) {
        if (error) *error = "profile lacks game_id or game_path";
        return std::nullopt;
    }
    return profile;
}

bool GameProfile::save(const std::filesystem::path& path, std::string* error) const {
    try {
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream stream(temporary, std::ios::trunc);
        if (!stream) throw std::runtime_error("cannot create temporary profile");
        stream << "# Kirikiri Vita per-game profile\n"
               << "game_id=" << game_id << '\n'
               << "game_path=" << game_path.string() << '\n'
               << "display_name=" << display_name << '\n'
               << "patch_title=" << patch_title << '\n'
               << "patch_brand=" << patch_brand << '\n'
               << "patch_commit=" << patch_commit << '\n'
               << "patch_root=" << patch_root.string() << '\n'
               << "xp3_filter_path=" << xp3_filter_path.string() << '\n'
               << "filter_origin=" << filter_origin << '\n'
               << "input_mapping_version=" << input.mapping_version << '\n'
               << "analog_deadzone=" << input.analog_deadzone << '\n'
               << "cursor_speed=" << input.cursor_speed << '\n'
               << "touch_enabled=" << (input.touch_enabled ? "true" : "false") << '\n';
        for (const auto& [source, action] : input.bindings) {
            stream << "bind." << source << '=' << action << '\n';
        }
        stream.close();
        if (!stream) throw std::runtime_error("failed writing profile");
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temporary, path);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

} // namespace krkrvita
