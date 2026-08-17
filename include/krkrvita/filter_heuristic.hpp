#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

struct FilterRule;

struct FilterBranch {
    std::vector<std::string> extensions;
    std::vector<std::string> path_prefixes;
    std::shared_ptr<FilterRule> rule;
};

struct FilterSample {
    std::uint32_t hash = 0;
    std::uint64_t offset = 0;
    std::string filename;
    std::vector<std::uint8_t> bytes;
};

enum class FilterOperation {
    Identity,
    XorHash,
    XorHashShift3,
    XorHashShift5,
    XorNotHashPlus1,
    XorConstant,
    XorHashShift,
    XorNotHashShift,
    XorHashShiftConstant,
    XorHashFoldConstant,
    XorPeriodic,
    XorOffsetAddConstant,
    XorHashOffsetParity,
    XorHashShiftByOffset,
    XorHashByteLanes,
    XorRotatingHash,
    XorLcgHash,
    XorThenAdd,
    XorThenNibbleSwap,
    RotateLeftByPopcount,
};

struct FilterRule {
    explicit FilterRule(FilterOperation selected = FilterOperation::Identity)
        : operation(selected) {}

    FilterOperation operation = FilterOperation::Identity;
    std::uint8_t constant = 0;
    std::uint8_t secondary = 0;
    std::uint8_t shift = 0;
    std::uint8_t modulus = 0;
    std::uint8_t post_add = 0;
    std::uint32_t seed_xor = 0;
    std::uint64_t start_offset = 0;
    std::uint64_t end_offset = ~std::uint64_t{0};
    std::vector<std::uint8_t> table;
    std::vector<FilterBranch> branches;
    int score = 0;
    int confidence = 0;

    std::string name() const;
    std::string to_tjs() const;
    void apply(std::uint32_t hash, std::uint64_t offset,
               std::span<std::uint8_t> bytes,
               std::string_view filename = {}) const;
};

enum class FilterInferenceDisposition {
    Detected,
    RequiresExecutableAnalysis,
    InsufficientArchiveEvidence,
};

struct FilterInferenceResult {
    FilterInferenceDisposition disposition =
        FilterInferenceDisposition::InsufficientArchiveEvidence;
    std::optional<FilterRule> rule;
    std::size_t constrained_samples = 0;
    std::string reason;
};

class FilterHeuristic {
public:
    static FilterInferenceResult analyze(const std::vector<FilterSample>& samples);
    static std::optional<FilterRule> detect(const std::vector<FilterSample>& samples);
    static int score_plaintext(std::string_view filename,
                               std::span<const std::uint8_t> bytes);
};

} // namespace krkrvita
