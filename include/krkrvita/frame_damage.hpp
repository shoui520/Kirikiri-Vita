#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace krkrvita {

struct FrameDamageRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    constexpr int width() const { return right - left; }
    constexpr int height() const { return bottom - top; }
    constexpr bool empty() const { return width() <= 0 || height() <= 0; }
};

// A bounding rectangle is intentional. Kirikiri normally supplies one dirty
// rectangle, and one moderately larger upload is cheaper than dozens of
// glTexSubImage2D calls when text produces adjacent glyph damage.
class FrameDamageRegion {
public:
    void configure(int width, int height) {
        width_ = std::max(0, width);
        height_ = std::max(0, height);
        clear();
    }

    void clear() {
        rect_ = {};
        empty_ = true;
    }

    void mark_full(int width, int height) {
        configure(width, height);
        if (width_ > 0 && height_ > 0) {
            rect_ = {0, 0, width_, height_};
            empty_ = false;
        }
    }

    void add(int left, int top, int right, int bottom) {
        if (width_ <= 0 || height_ <= 0) return;
        FrameDamageRect next{
            std::clamp(left, 0, width_), std::clamp(top, 0, height_),
            std::clamp(right, 0, width_), std::clamp(bottom, 0, height_)};
        if (next.empty()) return;
        if (empty_) {
            rect_ = next;
            empty_ = false;
            return;
        }
        rect_.left = std::min(rect_.left, next.left);
        rect_.top = std::min(rect_.top, next.top);
        rect_.right = std::max(rect_.right, next.right);
        rect_.bottom = std::max(rect_.bottom, next.bottom);
    }

    void merge(const FrameDamageRegion& other) {
        if (other.empty()) return;
        if (width_ != other.width_ || height_ != other.height_)
            mark_full(other.width_, other.height_);
        else
            add(other.rect_.left, other.rect_.top,
                other.rect_.right, other.rect_.bottom);
    }

    bool empty() const { return empty_; }
    bool full() const {
        return !empty_ && rect_.left == 0 && rect_.top == 0 &&
               rect_.right == width_ && rect_.bottom == height_;
    }
    int surface_width() const { return width_; }
    int surface_height() const { return height_; }
    const FrameDamageRect& rect() const { return rect_; }

private:
    FrameDamageRect rect_{};
    int width_ = 0;
    int height_ = 0;
    bool empty_ = true;
};

template <std::size_t BufferCount>
class PresentationDamageTracker {
    static_assert(BufferCount > 0, "presentation needs at least one texture");

public:
    void configure(int width, int height) {
        width_ = width;
        height_ = height;
        index_ = 0;
        for (FrameDamageRegion& damage : pending_)
            damage.mark_full(width, height);
    }

    void add_frame(const FrameDamageRegion& damage) {
        if (damage.surface_width() != width_ ||
            damage.surface_height() != height_) {
            configure(damage.surface_width(), damage.surface_height());
            return;
        }
        for (FrameDamageRegion& pending : pending_) pending.merge(damage);
    }

    const FrameDamageRegion& current() const { return pending_[index_]; }
    std::size_t current_index() const { return index_; }

    void complete_current() {
        pending_[index_].configure(width_, height_);
        index_ = (index_ + 1) % BufferCount;
    }

private:
    std::array<FrameDamageRegion, BufferCount> pending_{};
    std::size_t index_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace krkrvita
