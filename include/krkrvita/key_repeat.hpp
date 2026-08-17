#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace krkrvita {

// Matches Yuri's Windows DirectInput keyboard-repeat contract: a held key
// waits 500 ms, then produces repeat key-down events every 30 ms. A bounded
// catch-up prevents a stalled frame from flooding Kirikiri's input queue.
class KeyboardRepeatScheduler {
public:
    static constexpr std::uint64_t hold_delay_us = 500000;
    static constexpr std::uint64_t interval_us = 30000;
    static constexpr int catch_up_limit = 10;

    void clear() { next_repeat_.fill(0); }

    void press(std::uint16_t key, std::uint64_t now_us) {
        if (key >= next_repeat_.size()) return;
        next_repeat_[key] = now_us + hold_delay_us + interval_us;
    }

    void release(std::uint16_t key) {
        if (key < next_repeat_.size()) next_repeat_[key] = 0;
    }

    template <typename Callback>
    void pump(std::uint64_t now_us, Callback&& callback) {
        for (std::size_t key = 0; key < next_repeat_.size(); ++key) {
            std::uint64_t& next = next_repeat_[key];
            if (!next || now_us < next) continue;
            const std::uint64_t due = 1 + (now_us - next) / interval_us;
            const int emit = static_cast<int>(std::min<std::uint64_t>(
                due, static_cast<std::uint64_t>(catch_up_limit)));
            // Drop repeats beyond the bounded catch-up just like Yuri's
            // tTVPKeyRepeatEmulator advances LastRepeatCount to the present.
            next += due * interval_us;
            for (int count = 0; count < emit; ++count)
                callback(static_cast<std::uint16_t>(key));
        }
    }

private:
    std::array<std::uint64_t, 256> next_repeat_{};
};

} // namespace krkrvita
