#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

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
};

struct FilterRule {
    FilterOperation operation = FilterOperation::Identity;
    std::uint8_t constant = 0;
    int score = 0;
    int confidence = 0;

    std::string name() const;
    std::string to_tjs() const;
    void apply(std::uint32_t hash, std::uint64_t offset,
               std::span<std::uint8_t> bytes) const;
};

class FilterHeuristic {
public:
    static std::optional<FilterRule> detect(const std::vector<FilterSample>& samples);
    static int score_plaintext(std::string_view filename,
                               std::span<const std::uint8_t> bytes);
};

} // namespace krkrvita
