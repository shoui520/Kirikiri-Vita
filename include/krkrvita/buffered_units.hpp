#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace krkrvita {

// Fixed-capacity aggregation for APIs whose underlying sink is expensive per
// call. Flush is supplied by the caller so this remains allocation-free and
// does not impose an exception or error-reporting policy on engine code.
template <typename Unit, std::size_t Capacity>
class BufferedUnits {
    static_assert(Capacity > 0);

public:
    template <typename Flush>
    void append(Unit value, Flush&& flush) {
        if (used_ == Capacity) flush_to(flush);
        units_[used_++] = value;
    }

    template <typename Flush>
    void append(const Unit* values, std::size_t count, Flush&& flush) {
        if (!values && count) throw std::invalid_argument("null unit input");
        while (count) {
            if (used_ == Capacity) flush_to(flush);
            const auto available = Capacity - used_;
            const auto amount = count < available ? count : available;
            if (amount > std::numeric_limits<std::size_t>::max() / sizeof(Unit))
                throw std::length_error("buffered unit byte count overflow");
            std::memcpy(units_.data() + used_, values,
                        amount * sizeof(Unit));
            used_ += amount;
            values += amount;
            count -= amount;
        }
    }

    template <typename Flush>
    void flush_to(Flush&& flush) {
        if (!used_) return;
        flush(units_.data(), used_);
        used_ = 0;
    }

    std::size_t buffered() const noexcept { return used_; }

private:
    std::array<Unit, Capacity> units_{};
    std::size_t used_ = 0;
};

} // namespace krkrvita
