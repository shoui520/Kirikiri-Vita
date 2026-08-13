#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace krkrvita {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    void update(std::string_view text) { update(text.data(), text.size()); }
    std::array<std::uint8_t, 32> finish();

    static std::string hex(const std::array<std::uint8_t, 32>& digest);

private:
    void transform(const std::uint8_t block[64]);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t byte_count_ = 0;
    std::size_t buffered_ = 0;
    bool finished_ = false;
};

} // namespace krkrvita

