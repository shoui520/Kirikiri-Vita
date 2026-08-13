#pragma once

#include "krkrvita/game.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace krkrvita {

struct FilterVerification {
    std::size_t samples = 0;
    std::size_t recognized = 0;
    int score = 0;
};

struct PreparedFilter {
    std::filesystem::path path;
    std::string origin;
};

bool verify_retail_filter(const GameDescriptor& game,
                          const std::filesystem::path& filter_path,
                          FilterVerification* verification = nullptr,
                          std::string* error = nullptr);

// Uses a game-local filter when present; otherwise samples the real archives,
// identifies a common extraction pattern, and writes a generated TJS filter.
std::optional<PreparedFilter> prepare_filter_fallback(
    const GameDescriptor& game, const std::filesystem::path& generated_root,
    std::string* error = nullptr);

} // namespace krkrvita
