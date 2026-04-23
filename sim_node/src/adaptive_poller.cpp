#include "hardpoll/adaptive_poller.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "hardpoll/time_utils.hpp"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace hardpoll {

const char* poll_mode_name(PollMode m) {
    switch (m) {
        case PollMode::Interrupt: return "interrupt";
        case PollMode::Software:  return "software";
        case PollMode::Fpga:      return "fpga";
        case PollMode::Adaptive:  return "adaptive";
    }
    return "unknown";
}

PollMode parse_poll_mode(const std::string& s) {
    if (s == "interrupt") return PollMode::Interrupt;
    if (s == "software")  return PollMode::Software;
    if (s == "fpga")      return PollMode::Fpga;
    if (s == "adaptive")  return PollMode::Adaptive;
    throw std::invalid_argument("unknown poll mode: " + s);
}

AdaptivePoller::AdaptivePoller(SensorSlot* slot, AdaptivePollerConfig cfg,
                               EventHandler handler)
    : slot_(slot), cfg_(cfg), handler_(std::move(handler)) {}

AdaptivePoller::~AdaptivePoller() { stop(); }

void AdaptivePoller::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] {
        try_pin_cpu();
        run();
    });
}

void AdaptivePoller::stop() {
    running_.store(false);
    // Nudge interrupt mode so it wakes up to observe the flag.
    interrupt_gen_.fetch_add(1);
    if (thread_.joinable()) thread_.join();
}

void AdaptivePoller::notify_interrupt() {
    interrupt_gen_.fetch_add(1, std::memory_order_release);
}

void AdaptivePoller::try_pin_cpu() {
#if defined(__linux__)
    if (cfg_.pin_cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cfg_.pin_cpu, &set);
    // Best-effort; ignore failures so tests work in restricted environments.
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
}

void AdaptivePoller::run() {
    switch (cfg_.mode) {
        case PollMode::Software:  run_busy_poll(); break;
        case PollMode::Fpga:      run_fpga();      break;
        case PollMode::Interrupt: run_interrupt(); break;
        case PollMode::Adaptive:  run_adaptive();  break;
    }
}

void AdaptivePoller::run_busy_poll() {
    std::uint64_t last_seen = slot_->sequence.load(std::memory_order_acquire);
    std::uint64_t iters = 0;
    while (running_.load(std::memory_order_relaxed)) {
        const auto seq = slot_->sequence.load(std::memory_order_acquire);
        ++iters;
        if (seq != last_seen) {
            const auto detect = now_ns();
            const auto produce = slot_->t_produce_ns.load(
                std::memory_order_relaxed);
            observed_.fetch_add(1, std::memory_order_relaxed);
            busy_iters_.fetch_add(iters, std::memory_order_relaxed);
            handler_(seq, produce, detect, iters);
            iters = 0;
            last_seen = seq;
        } else {
            cpu_relax();
        }
    }
}

// Simulated FPGA/NIC polling: a tight loop that avoids even cpu_relax'ing so
// the perceived detection latency is as close to memory-access time as
// possible. In a real system this would be DMA descriptor polling.
void AdaptivePoller::run_fpga() {
    std::uint64_t last_seen = slot_->sequence.load(std::memory_order_acquire);
    std::uint64_t iters = 0;
    while (running_.load(std::memory_order_relaxed)) {
        const auto seq = slot_->sequence.load(std::memory_order_acquire);
        ++iters;
        if (seq != last_seen) {
            const auto detect = now_ns();
            const auto produce = slot_->t_produce_ns.load(
                std::memory_order_relaxed);
            observed_.fetch_add(1, std::memory_order_relaxed);
            busy_iters_.fetch_add(iters, std::memory_order_relaxed);
            handler_(seq, produce, detect, iters);
            iters = 0;
            last_seen = seq;
        }
    }
}

void AdaptivePoller::run_adaptive() {
    std::uint64_t last_seen = slot_->sequence.load(std::memory_order_acquire);
    std::uint64_t iters = 0;
    std::uint64_t idle = 0;
    std::uint64_t backoff_ns = cfg_.backoff_sleep_ns;
    while (running_.load(std::memory_order_relaxed)) {
        const auto seq = slot_->sequence.load(std::memory_order_acquire);
        ++iters;
        if (seq != last_seen) {
            const auto detect = now_ns();
            const auto produce = slot_->t_produce_ns.load(
                std::memory_order_relaxed);
            observed_.fetch_add(1, std::memory_order_relaxed);
            busy_iters_.fetch_add(iters, std::memory_order_relaxed);
            handler_(seq, produce, detect, iters);
            iters = 0;
            idle = 0;
            backoff_ns = cfg_.backoff_sleep_ns;
            last_seen = seq;
        } else {
            ++idle;
            if (idle >= cfg_.idle_iterations_before_backoff) {
                backoffs_.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(
                    std::chrono::nanoseconds(backoff_ns));
                backoff_ns = std::min(backoff_ns * 2, cfg_.max_backoff_sleep_ns);
                idle = 0;
            } else {
                cpu_relax();
            }
        }
    }
}

void AdaptivePoller::run_interrupt() {
    std::uint64_t last_seen = slot_->sequence.load(std::memory_order_acquire);
    std::uint64_t last_gen = interrupt_gen_.load();
    while (running_.load(std::memory_order_relaxed)) {
        // Spin until the interrupt generation changes, modelling a parked
        // thread that a hardware IRQ would wake up.
        std::uint64_t gen = interrupt_gen_.load(std::memory_order_acquire);
        while (running_.load(std::memory_order_relaxed) && gen == last_gen) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            gen = interrupt_gen_.load(std::memory_order_acquire);
        }
        if (!running_.load()) break;
        last_gen = gen;
        // Simulated wake latency (context switch + IRQ dispatch).
        busy_wait_ns(cfg_.interrupt_wake_ns);

        const auto seq = slot_->sequence.load(std::memory_order_acquire);
        if (seq == last_seen) continue;
        const auto detect = now_ns();
        const auto produce =
            slot_->t_produce_ns.load(std::memory_order_relaxed);
        observed_.fetch_add(1, std::memory_order_relaxed);
        handler_(seq, produce, detect, /*iterations=*/1);
        last_seen = seq;
    }
}

}  // namespace hardpoll
