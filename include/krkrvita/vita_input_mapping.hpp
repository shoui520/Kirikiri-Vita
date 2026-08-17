#pragma once

#include <map>
#include <string>

namespace krkrvita {

inline constexpr int vita_input_mapping_version = 4;

// The common controller layout used by every Vita backend and every newly
// generated profile. Mouse button actions use the current engine pointer
// position, so L clicks wherever the left stick cursor is.
inline std::map<std::string, std::string> vita_default_input_bindings() {
    return {
        {"cross", "mouse_right"},
        {"circle", "key_enter"},
        {"square", "disabled"},
        {"triangle", "mouse_wheel_up"},
        {"ltrigger", "mouse_left"},
        {"rtrigger", "key_control"},
        {"dpad_up", "key_up"},
        {"dpad_down", "key_down"},
        {"dpad_left", "key_left"},
        {"dpad_right", "key_right"},
        {"start", "disabled"},
        {"select", "disabled"},
        {"left_stick", "mouse_cursor"},
        {"front_touch", "mouse_absolute"},
    };
}

// Versions 1 through 3 serialized every then-default binding into each profile.
// Treat any historical default as inherited when loading those profiles,
// while retaining actions which were genuine per-game customizations.
inline bool vita_input_binding_is_superseded_default(
    int mapping_version, const std::string& source, const std::string& action) {
    if (mapping_version >= vita_input_mapping_version) return false;

    static const std::map<std::string, std::string> version_1_defaults = {
        {"cross", "mouse_left"}, {"circle", "mouse_right"},
        {"square", "key_space"}, {"triangle", "key_escape"},
        {"ltrigger", "key_pageup"}, {"rtrigger", "key_pagedown"},
        {"dpad_up", "key_up"}, {"dpad_down", "key_down"},
        {"dpad_left", "key_left"}, {"dpad_right", "key_right"},
        {"start", "key_enter"}, {"select", "menu"},
        {"left_stick", "mouse_cursor"},
        {"front_touch", "mouse_absolute"},
    };
    static const std::map<std::string, std::string> version_2_defaults = {
        {"cross", "mouse_right"}, {"circle", "key_enter"},
        {"square", "key_space"}, {"triangle", "mouse_wheel_up"},
        {"ltrigger", "key_pageup"}, {"rtrigger", "key_control"},
        {"dpad_up", "key_up"}, {"dpad_down", "key_down"},
        {"dpad_left", "key_left"}, {"dpad_right", "key_right"},
        {"start", "key_enter"}, {"select", "key_escape"},
        {"left_stick", "mouse_cursor"},
        {"front_touch", "mouse_absolute"},
    };
    static const std::map<std::string, std::string> version_3_defaults = {
        {"cross", "mouse_left"}, {"circle", "mouse_right"},
        {"square", "key_space"}, {"triangle", "key_escape"},
        {"ltrigger", "mouse_left"}, {"rtrigger", "key_pagedown"},
        {"dpad_up", "key_up"}, {"dpad_down", "key_down"},
        {"dpad_left", "key_left"}, {"dpad_right", "key_right"},
        {"start", "key_enter"}, {"select", "menu"},
        {"left_stick", "mouse_cursor"},
        {"front_touch", "mouse_absolute"},
    };

    const auto matches = [&](const auto& defaults) {
        const auto found = defaults.find(source);
        return found != defaults.end() && found->second == action;
    };
    return matches(version_1_defaults) || matches(version_2_defaults) ||
           matches(version_3_defaults);
}

} // namespace krkrvita
