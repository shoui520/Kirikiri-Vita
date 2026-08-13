#pragma once

#include "krkrvita/profile.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

struct PointerState {
    float x = 0;
    float y = 0;
    bool down = false;
};

struct InputSnapshot {
    std::uint32_t buttons = 0;
    std::uint32_t pressed = 0;
    std::uint32_t released = 0;
    float analog_x = 0;
    float analog_y = 0;
    PointerState touch;
};

class VitaInput {
public:
    VitaInput();
    ~VitaInput();
    InputSnapshot poll();

private:
    std::uint32_t previous_buttons_ = 0;
    float touch_min_x_ = 0;
    float touch_min_y_ = 0;
    float touch_span_x_ = 1920;
    float touch_span_y_ = 1088;
};

class VitaRenderer {
public:
    VitaRenderer();
    ~VitaRenderer();
    VitaRenderer(const VitaRenderer&) = delete;
    VitaRenderer& operator=(const VitaRenderer&) = delete;

    bool initialize(std::string* error = nullptr);
    bool system_font_ready() const;
    void begin();
    void rectangle(float x, float y, float width, float height,
                   float red, float green, float blue, float alpha = 1.0f);
    void text(float x, float y, float scale, std::string_view value,
              float red = 1, float green = 1, float blue = 1);
    void end();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class VitaAudio {
public:
    VitaAudio() = default;
    ~VitaAudio();
    bool initialize(std::string* error = nullptr);

private:
    void* device_ = nullptr;
    void* context_ = nullptr;
};

class EngineRuntime {
public:
    virtual ~EngineRuntime() = default;
    virtual bool start(const GameProfile& profile, std::string* error) = 0;
    virtual bool running() const = 0;
    virtual void input(const InputSnapshot& snapshot) = 0;
    virtual void frame() = 0;
    virtual void stop() = 0;
};

EngineRuntime& yuri_runtime();

} // namespace krkrvita
