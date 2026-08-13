#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace krkrvita {

// Executes Kirikiroid2Yuri-compatible xp3filter.tjs scripts. A separate TJS
// engine is created per extraction thread because retail games commonly decode
// images asynchronously.
class Xp3FilterVm {
public:
    Xp3FilterVm();
    ~Xp3FilterVm();
    Xp3FilterVm(Xp3FilterVm&&) noexcept;
    Xp3FilterVm& operator=(Xp3FilterVm&&) noexcept;
    Xp3FilterVm(const Xp3FilterVm&) = delete;
    Xp3FilterVm& operator=(const Xp3FilterVm&) = delete;

    bool load(std::string script, std::string* error = nullptr);
    bool active() const;
    bool decode(std::uint32_t file_hash, std::uint64_t offset,
                std::span<std::uint8_t> bytes, std::string_view filename,
                std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace krkrvita

