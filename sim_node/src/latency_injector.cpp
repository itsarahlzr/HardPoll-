#include "hardpoll/latency_injector.hpp"

#include <thread>

#include "hardpoll/time_utils.hpp"

namespace hardpoll {

LatencyInjector::LatencyInjector(LatencyInjectorConfig cfg)
    : cfg_(cfg), rng_(cfg.seed) {}

std::uint64_t LatencyInjector::inject() {
    std::uint64_t jitter = 0;
    if (cfg_.jitter_ns > 0) {
        std::uniform_int_distribution<std::uint64_t> d(0, cfg_.jitter_ns);
        jitter = d(rng_);
    }
    const std::uint64_t total = cfg_.base_ns + jitter;
    if (cfg_.emulate_fpga) {
        busy_wait_ns(total);
    } else {
        std::this_thread::sleep_for(std::chrono::nanoseconds(total));
    }
    return total;
}

}  // namespace hardpoll
