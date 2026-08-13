#pragma once

#include "krkrvita/game.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace krkrvita {

struct EmbeddedIcon {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t bit_depth = 0;
    std::vector<std::uint8_t> bytes;
};

class PeResources {
public:
    explicit PeResources(const std::filesystem::path& executable);

    bool valid() const { return valid_; }
    const std::string& error() const { return error_; }
    PeMetadata metadata() const;
    std::optional<EmbeddedIcon> largest_icon() const;

private:
    struct Section {
        std::uint32_t virtual_address = 0;
        std::uint32_t virtual_size = 0;
        std::uint32_t raw_offset = 0;
        std::uint32_t raw_size = 0;
    };

    std::optional<std::vector<std::uint8_t>> resource(std::uint32_t type,
                                                       std::uint32_t name) const;
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>
    resources(std::uint32_t type) const;
    std::optional<std::size_t> rva_to_offset(std::uint32_t rva) const;

    std::vector<std::uint8_t> image_;
    std::vector<Section> sections_;
    std::size_t resource_base_ = 0;
    bool valid_ = false;
    std::string error_;
};

} // namespace krkrvita

