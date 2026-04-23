#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <random>
#include <thread>

namespace hardpoll {

using InterruptNotifier = std::function<void()>;

// A simulated sensor writes a monotonically-increasing sequence to a shared
// slot. The adaptive poller polls this slot. This models a DMA ring or memory
// mapped peripheral where the hardware writes without raising an interrupt.
struct SensorSlot {
    // Written by sensor, read by poller. Single-writer / single-reader.
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint64_t> t_produce_ns{0};
    std::atomic<std::uint64_t> payload{0};
};

struct SensorConfig {
    // Target event rate in events per second. If 0, produce as fast as possible.
    double rate_hz = 1000.0;
    // Optional random jitter applied to inter-arrival time (fraction of period).
    double jitter_fraction = 0.0;
    // Stop after this many events (0 = unlimited).
    std::uint64_t max_events = 0;
    unsigned seed = 42;
};

class Sensor {
public:
    Sensor(SensorSlot* slot, SensorConfig cfg);
    ~Sensor();

    void set_interrupt_notifier(InterruptNotifier n) {
        notifier_ = std::move(n);
    }
    void start();
    void stop();
    std::uint64_t events_produced() const { return produced_.load(); }

private:
    void run();

    SensorSlot* slot_;
    SensorConfig cfg_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> produced_{0};
    std::thread thread_;
    std::mt19937 rng_;
    InterruptNotifier notifier_;
};

}  // namespace hardpoll
