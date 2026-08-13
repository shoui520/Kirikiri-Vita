#pragma once

#include "krkrvita/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

// Unified retail-game storage. Loose patch/game files and every XP3 archive
// are exposed through the same case-insensitive, slash-normalized namespace.
// XP3 filtering happens before Kirikiri text decoding.
class GameStorage {
public:
    GameStorage();
    ~GameStorage();
    GameStorage(GameStorage&&) noexcept;
    GameStorage& operator=(GameStorage&&) noexcept;
    GameStorage(const GameStorage&) = delete;
    GameStorage& operator=(const GameStorage&) = delete;

    static std::optional<GameStorage> mount(const GameProfile& profile,
                                            std::string* error = nullptr);

    void add_auto_path(std::string_view path);
    bool exists(std::string_view storage_name) const;
    std::optional<std::vector<std::uint8_t>> read(
        std::string_view storage_name, std::size_t safety_limit = 256u * 1024u * 1024u,
        std::string* error = nullptr) const;
    std::optional<std::string> read_script(std::string_view storage_name,
                                           std::string* error = nullptr) const;
    const std::vector<std::string>& auto_paths() const;

private:
    struct Impl;
    explicit GameStorage(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace krkrvita
