#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Yuri exposes a Cocos texture adapter on iTVPTexture2D even when the software
// renderer is used.  The Vita backend never consumes that adapter, but keeping
// this small storage-only implementation lets us compile Yuri's renderer
// without linking any Cocos frontend code.
namespace cocos2d {

struct Size {
    float width = 0.0f;
    float height = 0.0f;
    static const Size ZERO;
};

inline const Size Size::ZERO{};

class Texture2D {
public:
    enum class PixelFormat { RGBA8888 };

    void autorelease() {}

    bool initWithData(const void* pixels, std::size_t length, PixelFormat,
                      int width, int height, const Size&) {
        if (width < 0 || height < 0) return false;
        width_ = width;
        height_ = height;
        pixels_.assign(length, 0);
        if (pixels && length) std::memcpy(pixels_.data(), pixels, length);
        return true;
    }

    void updateWithData(const void* pixels, int x, int y, int width,
                        int height) {
        if (!pixels || x < 0 || y < 0 || width <= 0 || height <= 0 ||
            x + width > width_ || y + height > height_)
            return;
        const auto* source = static_cast<const std::uint8_t*>(pixels);
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
        const std::size_t required =
            static_cast<std::size_t>(width_) * height_ * 4;
        if (pixels_.size() < required) pixels_.resize(required);
        for (int row = 0; row < height; ++row) {
            std::memcpy(pixels_.data() +
                            (static_cast<std::size_t>(y + row) * width_ + x) * 4,
                        source + static_cast<std::size_t>(row) * row_bytes,
                        row_bytes);
        }
    }

    int getPixelsWide() const { return width_; }
    int getPixelsHigh() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

} // namespace cocos2d
