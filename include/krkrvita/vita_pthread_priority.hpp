#pragma once

namespace krkrvita {

// VitaSDK's pthread-embedded port exposes one SCHED_OTHER priority range.
// It records 128 as its lowest pthread priority, 160 as its default, and 191
// as its highest.  The backend inverts those values when it calls the Vita
// kernel, where a smaller number has higher scheduling priority.
constexpr int kVitaPthreadPriorityMin = 128;
constexpr int kVitaPthreadPriorityDefault = 160;
constexpr int kVitaPthreadPriorityMax = 191;
constexpr int kYuriThreadPriorityCount = 7;

constexpr int clamp_vita_pthread_priority_rank(int rank) {
    return rank < 0 ? 0
                    : (rank >= kYuriThreadPriorityCount
                           ? kYuriThreadPriorityCount - 1
                           : rank);
}

// Preserve all seven ordered Yuri priority classes while spanning the exact
// pthread-embedded range.  Rounding to nearest keeps Yuri's normal class at
// pthread priority 160, the VitaSDK default.
constexpr int vita_pthread_priority_for_yuri_rank(int rank) {
    const int bounded = clamp_vita_pthread_priority_rank(rank);
    constexpr int intervals = kYuriThreadPriorityCount - 1;
    constexpr int range =
        kVitaPthreadPriorityMax - kVitaPthreadPriorityMin;
    return kVitaPthreadPriorityMin +
           (range * bounded + intervals / 2) / intervals;
}

constexpr int vita_yuri_rank_for_pthread_priority(int priority) {
    const int bounded =
        priority < kVitaPthreadPriorityMin
            ? kVitaPthreadPriorityMin
            : (priority > kVitaPthreadPriorityMax
                   ? kVitaPthreadPriorityMax
                   : priority);
    constexpr int intervals = kYuriThreadPriorityCount - 1;
    constexpr int range =
        kVitaPthreadPriorityMax - kVitaPthreadPriorityMin;
    return ((bounded - kVitaPthreadPriorityMin) * intervals + range / 2) /
           range;
}

constexpr int vita_native_priority_for_pthread_priority(int priority) {
    return kVitaPthreadPriorityMin + kVitaPthreadPriorityMax - priority;
}

} // namespace krkrvita
