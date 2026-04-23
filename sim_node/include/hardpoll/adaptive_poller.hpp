#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "hardpoll/sensor.hpp"

namespace hardpoll {

enum class PollMode {
    Interrupt,   // baseline: condition variable, relies on kernel wakeup
    Software,    // busy-poll on a pinned core
    Fpga,        // simulated hardware-accelerated polling (tight loop w/o syscalls)
    Adaptive,    // start in software busy-poll, back off to short sleeps when idle
};

const char* poll_mode_name(PollMode m);
PollMode parse_poll_mode(const std::string& s);

struct AdaptivePollerConfig {
    PollMode mode = PollMode::Adaptive;
    // When adaptive: number of empty iterations before backing off.
    std::uint64_t idle_iterations_before_backoff = 1024;
    // Backoff sleep in nanoseconds when no events are observed.
    std::uint64_t backoff_sleep_ns = 2'000;  // 2 us
    // Maximum backoff sleep (exponential clamp).
    std::uint64_t max_backoff_sleep_ns = 100'000;  // 100 us
    // Core id to pin the polling thread to. -1 means no pinning.
    int pin_cpu = -1;
    // Interrupt mode: simulated wake latency budget (for modelling).
    std::uint64_t interrupt_wake_ns = 8'000;  // 8 us typical IRQ path
};

// Handler invoked for every detected event. Receives timings in ns.
using EventHandler =
    std::function<void(std::uint64_t sequence, std::uint64_t t_produce_ns,
                       std::uint64_t t_detect_ns, std::uint64_t iterations)>;

class AdaptivePoller {
public:
    AdaptivePoller(SensorSlot* slot, AdaptivePollerConfig cfg,
                   EventHandler handler);
    ~AdaptivePoller();

    void start();
    void stop();

    // Used by Interrupt mode to deliver a simulated wakeup.
    void notify_interrupt();

    std::uint64_t events_observed() const { return observed_.load(); }
    std::uint64_t busy_iterations() const { return busy_iters_.load(); }
    std::uint64_t backoff_events() const { return backoffs_.load(); }
    const AdaptivePollerConfig& config() const { return cfg_; }

private:
    void run();
    void run_busy_poll();
    void run_adaptive();
    void run_interrupt();
    void run_fpga();
    void try_pin_cpu();

    SensorSlot* slot_;
    AdaptivePollerConfig cfg_;
    EventHandler handler_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> observed_{0};
    std::atomic<std::uint64_t> busy_iters_{0};
    std::atomic<std::uint64_t> backoffs_{0};
    std::atomic<std::uint64_t> interrupt_gen_{0};
    std::thread thread_;
};

}  // namespace hardpoll
