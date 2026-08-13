#pragma once

#include "krkrvita/game.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

inline constexpr std::string_view kPatchCommit =
    "0af1bafdb7a031e4c74fd4b320b6a0a1ec03b300";
inline constexpr std::string_view kPatchRawBase =
    "https://raw.githubusercontent.com/zeas2/Kirikiroid2_patch/";

struct PatchEntry {
    std::uint64_t timestamp = 0;
    std::string brand;
    std::string canonical_title;
    std::string display_title;
    std::vector<std::string> files;
};

class PatchManifest {
public:
    static PatchManifest parse(std::string_view javascript);
    static PatchManifest load(const std::filesystem::path& path);

    const std::vector<PatchEntry>& entries() const { return entries_; }

private:
    std::vector<PatchEntry> entries_;
};

struct PatchCandidate {
    const PatchEntry* entry = nullptr;
    int score = 0;
    std::vector<std::string> reasons;
};

struct PatchResolution {
    std::vector<PatchCandidate> candidates;
    bool automatic = false;

    const PatchCandidate* best() const {
        return candidates.empty() ? nullptr : &candidates.front();
    }
};

class PatchResolver {
public:
    static PatchResolution resolve(const GameDescriptor& game,
                                   const PatchManifest& manifest);
};

bool is_safe_patch_path(std::string_view relative);
std::string patch_file_url(std::string_view relative);

} // namespace krkrvita

