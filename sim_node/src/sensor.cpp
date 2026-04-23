#include "hardpoll/sensor.hpp"

#include <algorithm>
#include <cmath>

#include "hardpoll/time_utils.hpp"

namespace hardpoll {

Sensor::Sensor(SensorSlot* slot, SensorConfig cfg)
    : slot_(slot), cfg_(cfg), rng_(cfg.seed) {}

Sensor::~Sensor() { stop(); }

void Sensor::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void Sensor::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void Sensor::run() {
    const double period_ns =
        cfg_.rate_hz > 0.0 ? 1e9 / cfg_.rate_hz : 0.0;
    std::uniform_real_distribution<double> jitter(-1.0, 1.0);

    auto next = Clock::now();
    std::uint64_t seq = 1;
    while (running_.load()) {
        if (period_ns > 0.0) {
            while (Clock::now() < next && running_.load()) {
                cpu_relax();
            }
        }
        const auto now = now_ns();
        slot_->t_produce_ns.store(now, std::memory_order_relaxed);
        slot_->payload.store(seq * 0x9E3779B97F4A7C15ull,
                             std::memory_order_relaxed);
        // Publish sequence last: the poller spins on this field.
        slot_->sequence.store(seq, std::memory_order_release);
        if (notifier_) notifier_();

        produced_.fetch_add(1, std::memory_order_relaxed);
        if (cfg_.max_events && seq >= cfg_.max_events) {
            running_.store(false);
            break;
        }
        ++seq;

        if (period_ns > 0.0) {
            double jit_ns = 0.0;
            if (cfg_.jitter_fraction > 0.0) {
                jit_ns = jitter(rng_) * cfg_.jitter_fraction * period_ns;
            }
            next += std::chrono::nanoseconds(
                static_cast<std::int64_t>(period_ns + jit_ns));
        }
    }
}

}  // namespace hardpoll
