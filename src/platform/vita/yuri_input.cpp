#include "tjsCommHead.h"

#include "krkrvita/key_repeat.hpp"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/touch_mapping.hpp"
#include "krkrvita/vita_input_mapping.hpp"
#include "tvpinputdefs.h"
#include "vkdefine.h"
#include "yuri_input.hpp"
#include "yuri_window_layer.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace {

struct InputConfiguration {
    std::map<std::string, std::string> bindings;
    float deadzone = 0.18f;
    float cursor_speed = 780.0f;
    bool touch_enabled = true;
};

InputConfiguration configuration;
std::uint32_t previous_buttons = 0;
std::uint64_t previous_tick = 0;
bool touch_down = false;
std::uint8_t touch_id = 0;
std::int32_t touch_x = 0;
std::int32_t touch_y = 0;
SceTouchPanelInfo touch_panel{};
std::atomic<bool> touch_reader_running{false};
bool touch_reader_started = false;
std::thread touch_reader_thread;
std::mutex touch_samples_mutex;
std::deque<SceTouchData> touch_samples;
constexpr std::size_t touch_sample_capacity = 256;
std::array<bool, 256> key_state{};
std::array<bool, 256> key_pressed{};
std::array<std::uint16_t, 256> key_hold_count{};
krkrvita::KeyboardRepeatScheduler key_repeats;
std::uint16_t current_pad_state = 0;
std::uint16_t pressed_pad_state = 0;

constexpr std::uint16_t pad_left = 1u << 0;
constexpr std::uint16_t pad_right = 1u << 1;
constexpr std::uint16_t pad_up = 1u << 2;
constexpr std::uint16_t pad_down = 1u << 3;
constexpr std::uint16_t pad_1 = 1u << 4;
constexpr std::uint16_t pad_2 = 1u << 5;
constexpr std::uint16_t pad_3 = 1u << 6;
constexpr std::uint16_t pad_4 = 1u << 7;
constexpr std::uint16_t pad_5 = 1u << 8;
constexpr std::uint16_t pad_6 = 1u << 9;
constexpr std::uint16_t pad_7 = 1u << 10;
constexpr std::uint16_t pad_8 = 1u << 11;

std::uint16_t vita_pad_state(std::uint32_t buttons) {
    std::uint16_t state = 0;
    if (buttons & SCE_CTRL_LEFT) state |= pad_left;
    if (buttons & SCE_CTRL_RIGHT) state |= pad_right;
    if (buttons & SCE_CTRL_UP) state |= pad_up;
    if (buttons & SCE_CTRL_DOWN) state |= pad_down;
    if (buttons & SCE_CTRL_CROSS) state |= pad_1;
    if (buttons & SCE_CTRL_CIRCLE) state |= pad_2;
    if (buttons & SCE_CTRL_SQUARE) state |= pad_3;
    if (buttons & SCE_CTRL_TRIANGLE) state |= pad_4;
    if (buttons & SCE_CTRL_LTRIGGER) state |= pad_5;
    if (buttons & SCE_CTRL_RTRIGGER) state |= pad_6;
    if (buttons & SCE_CTRL_START) state |= pad_7;
    if (buttons & SCE_CTRL_SELECT) state |= pad_8;
    return state;
}

std::uint16_t virtual_pad_bit(tjs_uint keycode) {
    switch (keycode) {
    case VK_PADLEFT: return pad_left;
    case VK_PADRIGHT: return pad_right;
    case VK_PADUP: return pad_up;
    case VK_PADDOWN: return pad_down;
    case VK_PAD1: return pad_1;
    case VK_PAD2: return pad_2;
    case VK_PAD3: return pad_3;
    case VK_PAD4: return pad_4;
    case VK_PAD5: return pad_5;
    case VK_PAD6: return pad_6;
    case VK_PAD7: return pad_7;
    case VK_PAD8: return pad_8;
    default: return 0;
    }
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parse_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "yes" ||
           value == "on";
}

void set_defaults() {
    configuration = {};
    configuration.bindings = krkrvita::vita_default_input_bindings();
}

void load_profile(const char* path) {
    if (!path || !*path) return;
    std::ifstream input(path);
    int mapping_version = 1;
    std::map<std::string, std::string> profile_bindings;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (key.rfind("bind.", 0) == 0)
            profile_bindings[key.substr(5)] = value;
        else if (key == "input_mapping_version")
            mapping_version = std::max(1, std::atoi(value.c_str()));
        else if (key == "analog_deadzone")
            configuration.deadzone =
                std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 0.95f);
        else if (key == "cursor_speed")
            configuration.cursor_speed =
                std::clamp(std::strtof(value.c_str(), nullptr), 1.0f, 4000.0f);
        else if (key == "touch_enabled")
            configuration.touch_enabled = parse_bool(value);
    }

    // Old profiles serialized every historical default. Let version 4's
    // global layout replace those inherited values, while preserving genuine
    // per-game custom actions.
    for (const auto& [source, action] : profile_bindings) {
        if (krkrvita::vita_input_binding_is_superseded_default(
                mapping_version, source, action))
            continue;
        configuration.bindings[source] = action;
    }
}

const std::string& binding(const char* source) {
    static const std::string empty;
    const auto found = configuration.bindings.find(source);
    return found == configuration.bindings.end() ? empty : found->second;
}

std::uint16_t action_key(const std::string& action) {
    if (action == "key_space") return VK_SPACE;
    if (action == "key_enter") return VK_RETURN;
    if (action == "key_escape" || action == "menu") return VK_ESCAPE;
    if (action == "key_pageup") return VK_PRIOR;
    if (action == "key_pagedown") return VK_NEXT;
    if (action == "key_up") return VK_UP;
    if (action == "key_down") return VK_DOWN;
    if (action == "key_left") return VK_LEFT;
    if (action == "key_right") return VK_RIGHT;
    if (action == "key_control") return VK_CONTROL;
    if (action == "key_shift") return VK_SHIFT;
    if (action == "key_tab") return VK_TAB;
    if (action == "key_backspace") return VK_BACK;
    return 0;
}

std::uint32_t current_shift_state() {
    std::uint32_t state = 0;
    if (key_state[VK_SHIFT]) state |= TVP_SS_SHIFT;
    if (key_state[VK_MENU]) state |= TVP_SS_ALT;
    if (key_state[VK_CONTROL]) state |= TVP_SS_CTRL;
    if (key_state[VK_LBUTTON]) state |= TVP_SS_LEFT;
    if (key_state[VK_RBUTTON]) state |= TVP_SS_RIGHT;
    if (key_state[VK_MBUTTON]) state |= TVP_SS_MIDDLE;
    return state;
}

bool is_arrow_key(std::uint16_t key) {
    return key == VK_UP || key == VK_DOWN || key == VK_LEFT ||
           key == VK_RIGHT;
}

void update_key(bool down, std::uint16_t key) {
    if (key >= key_state.size()) return;
    if (down) {
        if (key_hold_count[key]++ != 0) return;
        key_state[key] = true;
        key_pressed[key] = true;
        key_repeats.press(key, sceKernelGetProcessTimeWide());
        krkrvita_yuri_key(true, key, current_shift_state());
        if (is_arrow_key(key)) {
            static bool arrow_reported = false;
            if (!arrow_reported) {
                krkrvita_boot_trace("vita-keyboard-arrow-dispatched");
                arrow_reported = true;
            }
        }
        return;
    }
    if (key_hold_count[key] == 0 || --key_hold_count[key] != 0) return;
    key_state[key] = false;
    key_repeats.release(key);
    krkrvita_yuri_key(false, key, current_shift_state());
}

void pump_key_repeats() {
    key_repeats.pump(sceKernelGetProcessTimeWide(), [](std::uint16_t key) {
        if (!key_state[key]) return;
        krkrvita_yuri_key(
            true, key, current_shift_state() | TVP_SS_REPEAT);
        static bool repeat_reported = false;
        if (!repeat_reported) {
            krkrvita_boot_trace("vita-keyboard-repeat-dispatched");
            repeat_reported = true;
        }
    });
}

void perform_action(const std::string& action, bool down) {
    if (action.empty() || action == "disabled" || action == "pad" ||
        action == "mouse_cursor" || action == "mouse_absolute")
        return;
    const std::uint16_t key = action_key(action);
    if (key) {
        update_key(down, key);
        return;
    }
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (!krkrvita_yuri_active_pointer(width, height, x, y)) return;
    if (action == "mouse_left")
        krkrvita_yuri_pointer_button(down, 0, x, y);
    else if (action == "mouse_right")
        krkrvita_yuri_pointer_button(down, 1, x, y);
    else if (action == "mouse_wheel_up" && down)
        krkrvita_yuri_pointer_wheel(120, x, y);
    else if (action == "mouse_wheel_down" && down)
        krkrvita_yuri_pointer_wheel(-120, x, y);
}

struct ButtonBinding {
    std::uint32_t mask;
    const char* name;
};

constexpr ButtonBinding buttons[] = {
    {SCE_CTRL_CROSS, "cross"}, {SCE_CTRL_CIRCLE, "circle"},
    {SCE_CTRL_SQUARE, "square"}, {SCE_CTRL_TRIANGLE, "triangle"},
    {SCE_CTRL_LTRIGGER, "ltrigger"}, {SCE_CTRL_RTRIGGER, "rtrigger"},
    {SCE_CTRL_UP, "dpad_up"}, {SCE_CTRL_DOWN, "dpad_down"},
    {SCE_CTRL_LEFT, "dpad_left"}, {SCE_CTRL_RIGHT, "dpad_right"},
    {SCE_CTRL_START, "start"}, {SCE_CTRL_SELECT, "select"},
};

void pump_controller() {
    SceCtrlData pad{};
    if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0) return;
    const std::uint16_t new_pad_state = vita_pad_state(pad.buttons);
    pressed_pad_state |= new_pad_state & ~current_pad_state;
    current_pad_state = new_pad_state;
    const std::uint32_t changed = pad.buttons ^ previous_buttons;
    for (const ButtonBinding& button : buttons) {
        if (changed & button.mask)
            perform_action(binding(button.name), (pad.buttons & button.mask) != 0);
    }
    previous_buttons = pad.buttons;

    if (binding("left_stick") != "mouse_cursor") return;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (!krkrvita_yuri_active_pointer(width, height, x, y)) return;
    float axis_x = (static_cast<int>(pad.lx) - 128) / 127.0f;
    float axis_y = (static_cast<int>(pad.ly) - 128) / 127.0f;
    const float magnitude = std::sqrt(axis_x * axis_x + axis_y * axis_y);
    if (magnitude <= configuration.deadzone) return;
    const float response = std::min(
        1.0f, (magnitude - configuration.deadzone) / (1.0f - configuration.deadzone));
    axis_x = axis_x / magnitude * response;
    axis_y = axis_y / magnitude * response;
    const std::uint64_t now = sceKernelGetProcessTimeWide();
    const float seconds = previous_tick
                              ? std::min(0.05f, (now - previous_tick) / 1000000.0f)
                              : 0.0f;
    x = std::clamp(x + static_cast<int>(axis_x * configuration.cursor_speed * seconds),
                   0, width - 1);
    y = std::clamp(y + static_cast<int>(axis_y * configuration.cursor_speed * seconds),
                   0, height - 1);
    krkrvita_yuri_pointer_move(x, y);
}

void dispatch_touch_sample(const SceTouchData& data) {
    if (data.reportNum == 0) {
        if (touch_down)
            krkrvita_yuri_pointer_button(false, 0, touch_x, touch_y);
        touch_down = false;
        return;
    }

    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t cursor_x = 0;
    std::int32_t cursor_y = 0;
    if (!krkrvita_yuri_active_pointer(width, height, cursor_x, cursor_y)) return;

    const SceTouchReport* report = nullptr;
    if (touch_down) {
        for (std::uint32_t index = 0; index < data.reportNum; ++index) {
            if (data.report[index].id == touch_id) {
                report = &data.report[index];
                break;
            }
        }
        if (!report) {
            krkrvita_yuri_pointer_button(false, 0, touch_x, touch_y);
            touch_down = false;
        }
    }
    if (!report) report = &data.report[0];

    const krkrvita::TouchPoint point = krkrvita::map_vita_touch_to_layer(
        report->x, report->y,
        {touch_panel.minDispX, touch_panel.minDispY,
         touch_panel.maxDispX, touch_panel.maxDispY},
        width, height);
    touch_x = point.x;
    touch_y = point.y;
    if (!touch_down)
        krkrvita_yuri_pointer_button(true, 0, touch_x, touch_y);
    else
        krkrvita_yuri_pointer_move(touch_x, touch_y);
    touch_id = report->id;
    touch_down = true;
}

void start_touch_reader() {
    if (touch_reader_started || !configuration.touch_enabled ||
        binding("front_touch") != "mouse_absolute")
        return;
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                             SCE_TOUCH_SAMPLING_STATE_START);
    touch_reader_running = true;
    touch_reader_started = true;
    touch_reader_thread = std::thread([] {
        std::array<SceTouchData, 64> samples{};
        while (touch_reader_running.load(std::memory_order_relaxed)) {
            const int count = sceTouchRead(SCE_TOUCH_PORT_FRONT,
                                           samples.data(), samples.size());
            if (!touch_reader_running.load(std::memory_order_relaxed)) break;
            if (count < 0) {
                sceKernelDelayThread(16000);
                continue;
            }
            std::lock_guard<std::mutex> lock(touch_samples_mutex);
            for (int index = 0; index < count; ++index) {
                if (touch_samples.size() == touch_sample_capacity)
                    touch_samples.pop_front();
                touch_samples.push_back(samples[index]);
            }
        }
    });
    krkrvita_boot_trace("vita-touch-history-reader-started");
}

void pump_touch() {
    start_touch_reader();
    if (!touch_reader_started) return;
    std::deque<SceTouchData> pending;
    {
        std::lock_guard<std::mutex> lock(touch_samples_mutex);
        pending.swap(touch_samples);
    }
    for (const SceTouchData& data : pending) dispatch_touch_sample(data);
}

} // namespace

void krkrvita_yuri_input_initialize(const char* profile_path) {
    set_defaults();
    load_profile(profile_path);
    previous_buttons = 0;
    key_state.fill(false);
    key_pressed.fill(false);
    key_hold_count.fill(0);
    key_repeats.clear();
    current_pad_state = 0;
    pressed_pad_state = 0;
    previous_tick = sceKernelGetProcessTimeWide();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &touch_panel) < 0) {
        // Sony defines the Vita front panel as 1920x1088 touch samples over
        // the complete display. Keep direct touch usable even if panel-info
        // retrieval transiently fails.
        touch_panel.minDispX = 0;
        touch_panel.minDispY = 0;
        touch_panel.maxDispX = 1919;
        touch_panel.maxDispY = 1087;
        krkrvita_boot_trace("vita-touch-panel-default-bounds");
    }
}

void krkrvita_yuri_input_pump() {
    pump_controller();
    pump_key_repeats();
    pump_touch();
    previous_tick = sceKernelGetProcessTimeWide();
}

void krkrvita_yuri_input_shutdown() {
    if (!touch_reader_started) return;
    touch_reader_running = false;
    if (touch_reader_thread.joinable()) touch_reader_thread.join();
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                             SCE_TOUCH_SAMPLING_STATE_STOP);
    {
        std::lock_guard<std::mutex> lock(touch_samples_mutex);
        touch_samples.clear();
    }
    touch_reader_started = false;
    touch_down = false;
}

bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent) {
    if (keycode >= key_state.size()) return false;
    if (getcurrent) return key_state[keycode];
    const bool result = key_pressed[keycode];
    key_pressed[keycode] = false;
    return result;
}

bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent) {
    const std::uint16_t mask =
        keycode == VK_PADANY ? static_cast<std::uint16_t>(~0u)
                            : virtual_pad_bit(keycode);
    if (!mask) return false;
    if (getcurrent) return (current_pad_state & mask) != 0;
    const bool result = (pressed_pad_state & mask) != 0;
    pressed_pad_state &= ~mask;
    return result;
}
