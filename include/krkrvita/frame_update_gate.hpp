#pragma once

#include <cstdint>

namespace krkrvita {

// Carries Yuri's draw-buffer update signal across the platform/event-loop
// boundary. A generation ticket prevents a successful presentation from
// consuming an update that arrived while that presentation was in progress.
class FrameUpdateGate {
public:
    using Ticket = std::uint64_t;

    void request() { ++requested_generation_; }

    [[nodiscard]] bool pending() const {
        return requested_generation_ != presented_generation_;
    }

    [[nodiscard]] Ticket begin() const {
        return pending() ? requested_generation_ : 0;
    }

    void complete(Ticket ticket, bool succeeded) {
        if (succeeded && ticket > presented_generation_ &&
            ticket <= requested_generation_)
            presented_generation_ = ticket;
    }

private:
    Ticket requested_generation_ = 0;
    Ticket presented_generation_ = 0;
};

} // namespace krkrvita
