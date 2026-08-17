#pragma once

#include <array>
#include <cstddef>

namespace krkrvita {

// Owns only the committed identity of the VitaGL software-presentation
// surface. Texture allocation happens into a separate candidate array; a
// partial allocation can therefore be discarded without changing the active
// dimensions or texture names. This also keeps an identical-size request
// eligible for a later retry.
template <std::size_t BufferCount>
class PresentationSurfaceState {
    static_assert(BufferCount > 0,
                  "presentation needs at least one texture");

public:
    using TextureArray = std::array<unsigned int, BufferCount>;

    struct CommitResult {
        bool committed = false;
        TextureArray retired{};
    };

    bool valid() const noexcept {
        return width_ > 0 && height_ > 0 && complete_ids(textures_);
    }

    bool matches(int width, int height) const noexcept {
        return valid() && width_ == width && height_ == height;
    }

    bool resize_required(int width, int height) const noexcept {
        return width > 0 && height > 0 && !matches(width, height);
    }

    CommitResult complete_resize(int width, int height,
                                 const TextureArray& candidates,
                                 std::size_t ready_count) noexcept {
        CommitResult result;
        if (width <= 0 || height <= 0 || ready_count != BufferCount ||
            !complete_ids(candidates))
            return result;

        result.retired = textures_;
        textures_ = candidates;
        width_ = width;
        height_ = height;
        result.committed = true;
        return result;
    }

    const TextureArray& textures() const noexcept { return textures_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

private:
    static bool complete_ids(const TextureArray& ids) noexcept {
        for (std::size_t index = 0; index < BufferCount; ++index) {
            if (ids[index] == 0) return false;
            for (std::size_t prior = 0; prior < index; ++prior) {
                if (ids[prior] == ids[index]) return false;
            }
        }
        return true;
    }

    TextureArray textures_{};
    int width_ = 0;
    int height_ = 0;
};

} // namespace krkrvita
