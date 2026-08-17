#include "krkrvita/threading_self_test.hpp"

#include "ThreadIntf.h"
#include "ThreadImpl.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

class SuspendedThreadProbe final : public tTVPThread {
public:
    SuspendedThreadProbe(std::mutex& mutex, std::condition_variable& condition,
                         bool& ran)
        : tTVPThread(true), mutex_(mutex), condition_(condition), ran_(ran) {}

protected:
    void Execute() override {
        std::lock_guard<std::mutex> lock(mutex_);
        ran_ = true;
        condition_.notify_one();
    }

private:
    std::mutex& mutex_;
    std::condition_variable& condition_;
    bool& ran_;
};

} // namespace

bool krkrvita_vita_threading_self_test() {
    constexpr int kRounds = 128;
    constexpr auto kTimeout = std::chrono::seconds(2);

    std::mutex mutex;
    std::condition_variable condition;
    int offered = 0;
    int acknowledged = 0;
    bool stop = false;
    bool passed = true;

    std::thread worker([&] {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stop) {
            if (!condition.wait_for(lock, kTimeout, [&] {
                    return stop || offered > acknowledged;
                })) {
                passed = false;
                stop = true;
                condition.notify_one();
                break;
            }
            if (stop) break;
            acknowledged = offered;
            condition.notify_one();
        }
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        for (int round = 1; round <= kRounds; ++round) {
            offered = round;
            condition.notify_one();
            if (!condition.wait_for(lock, kTimeout, [&] {
                    return stop || acknowledged == round;
                }) || stop) {
                passed = false;
                break;
            }
        }
        stop = true;
        condition.notify_one();
    }

    worker.join();
    if (!passed || acknowledged != kRounds) return false;

    // Verify Yuri's own auto-reset event, including the signal-before-wait
    // case that its old bare-condition-variable implementation lost.
    tTVPThreadEvent event;
    event.Set();
    const auto started = std::chrono::steady_clock::now();
    event.WaitFor(250);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed >= std::chrono::milliseconds(100)) return false;

    bool yuri_wait_finished = false;
    std::thread yuri_waiter([&] {
        event.WaitFor(1000);
        std::lock_guard<std::mutex> lock(mutex);
        yuri_wait_finished = true;
        condition.notify_one();
    });
    event.Set();
    {
        std::unique_lock<std::mutex> lock(mutex);
        passed = condition.wait_for(lock, kTimeout,
                                    [&] { return yuri_wait_finished; });
    }
    if (!passed) event.Set();
    yuri_waiter.join();
    if (!passed) return false;

    // KAG timers are suspended tTVPThreads resumed immediately after their
    // constructor. Exercise that exact signal-before-wait race repeatedly.
    // A second Resume is used only to recover a failed probe for a clean join.
    constexpr int kSuspendedThreadRounds = 32;
    for (int round = 0; round < kSuspendedThreadRounds; ++round) {
        bool ran = false;
        SuspendedThreadProbe probe(mutex, condition, ran);
        probe.Resume();
        {
            std::unique_lock<std::mutex> lock(mutex);
            passed = condition.wait_for(lock, kTimeout, [&] { return ran; });
        }
        if (!passed) probe.Resume();
        probe.WaitFor();
        if (!passed) return false;
    }
    return true;
}
