#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace krkrvita {

// Archive-only Phase 1 result.  This interface deliberately exposes no C++20
// types so the C++17 Yuri launch edge can consume the C++20 heuristic engine
// without changing Yuri's compilation contract.
struct Phase1FilterResult {
    std::string archive_fingerprint;
    std::string rule_name;
    std::string script;
    std::size_t samples = 0;
    std::size_t recognized = 0;
};

std::optional<Phase1FilterResult> infer_phase1_filter(
    const std::string& game_path, std::string* error = nullptr);

} // namespace krkrvita
