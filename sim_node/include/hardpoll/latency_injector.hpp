#pragma once

#include <cstdint>
#include <random>

namespace hardpoll {

// Simulates the FPGA/NIC injection board from the diagram: deterministic
// latency plus optional jitter. Delays are implemented with a tight busy
// wait so we don't re-introduce OS scheduling noise.
struct LatencyInjectorConfig {
    std::uint64_t base_ns = 500;       // deterministic one-way latency
    std::uint64_t jitter_ns = 50;      // uniform jitter [0, jitter_ns]
    bool emulate_fpga = true;          // if false, uses std::this_thread::sleep_for
    unsigned seed = 1337;
};

class LatencyInjector {
public:
    explicit LatencyInjector(LatencyInjectorConfig cfg);

    // Blocks for base + jitter and returns the applied delay in ns.
    std::uint64_t inject();

    const LatencyInjectorConfig& config() const { return cfg_; }

private:
    LatencyInjectorConfig cfg_;
    std::mt19937 rng_;
};

}  // namespace hardpoll
