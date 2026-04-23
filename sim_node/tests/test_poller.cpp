// Minimal self-checking tests for the adaptive poller and latency injector.
// No framework dependency: exits non-zero on failure.

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

#include "hardpoll/adaptive_poller.hpp"
#include "hardpoll/latency_injector.hpp"
#include "hardpoll/node.hpp"
#include "hardpoll/sensor.hpp"

namespace {

int check_mode(hardpoll::PollMode mode, const char* name) {
    hardpoll::NodeConfig cfg;
    cfg.node_id = std::string("test-") + name;
    cfg.trace_path = std::string("/tmp/hardpoll_test_") + name + ".jsonl";
    cfg.sensor.rate_hz = 5000.0;
    cfg.sensor.max_events = 200;
    cfg.poller.mode = mode;
    cfg.poller.idle_iterations_before_backoff = 64;
    cfg.poller.backoff_sleep_ns = 1000;
    cfg.injector.base_ns = 200;
    cfg.injector.jitter_ns = 50;

    hardpoll::Node node(cfg);
    if (!node.start()) {
        std::fprintf(stderr, "mode %s: node.start() failed\n", name);
        return 1;
    }
    // Cap wall-clock wait at ~2s to avoid hangs.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (node.events_published() < 200 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    node.stop();
    const auto pub = node.events_published();
    std::printf("mode %-10s published=%llu\n", name,
                static_cast<unsigned long long>(pub));
    if (pub < 50) {
        std::fprintf(stderr, "mode %s: too few events (%llu)\n", name,
                     static_cast<unsigned long long>(pub));
        return 1;
    }
    return 0;
}

int check_latency_injector() {
    hardpoll::LatencyInjectorConfig cfg;
    cfg.base_ns = 1000;
    cfg.jitter_ns = 200;
    hardpoll::LatencyInjector inj(cfg);
    for (int i = 0; i < 32; ++i) {
        const auto d = inj.inject();
        if (d < cfg.base_ns || d > cfg.base_ns + cfg.jitter_ns) {
            std::fprintf(stderr, "injector delay out of range: %llu\n",
                         static_cast<unsigned long long>(d));
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main() {
    int rc = 0;
    rc |= check_latency_injector();
    rc |= check_mode(hardpoll::PollMode::Software, "software");
    rc |= check_mode(hardpoll::PollMode::Fpga, "fpga");
    rc |= check_mode(hardpoll::PollMode::Adaptive, "adaptive");
    rc |= check_mode(hardpoll::PollMode::Interrupt, "interrupt");
    if (rc == 0) std::printf("ALL TESTS PASSED\n");
    return rc;
}
