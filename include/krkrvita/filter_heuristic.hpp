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
    // Additive counterparts. Kirikiri filters are not all involutions: a
    // filter may add on write and subtract on read. These operations are the
    // decode direction, matching every other operation here.
    SubConstant,
    XorHashMultiply,
    SubHashMultiply,
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
    std::uint8_t multiplier = 1;
    std::uint32_t seed_xor = 0;
    std::uint64_t start_offset = 0;
    std::uint64_t end_offset = ~std::uint64_t{0};
    std::vector<std::uint8_t> table;
    // Dispatch by logical file name. Mutually exclusive with `segments`.
    std::vector<FilterBranch> branches;
    // Dispatch by absolute offset within the file. Each entry carries its own
    // [start_offset, end_offset) window and is applied only inside it, so a
    // file can be decoded by different transforms in different regions. The
    // last entry is normally unbounded and covers the remainder of the file.
    std::vector<std::shared_ptr<FilterRule>> segments;
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
    // How much of a decoded payload agrees with its declared format beyond the
    // signature bytes: 1.0 when every checkable structure holds, less when the
    // content stops making sense partway through, and nothing when the payload
    // cannot be judged past its header. Reported by `xp3-diagnose` so a rule
    // that only decodes file headers is visible rather than merely low-scoring.
    static std::optional<double> format_agreement(std::string_view filename,
                                                  std::span<const std::uint8_t> bytes);
};

} // namespace krkrvita
