#pragma once

#include <memory>
#include <string>

#include "hardpoll/adaptive_poller.hpp"
#include "hardpoll/latency_injector.hpp"
#include "hardpoll/sensor.hpp"
#include "hardpoll/trace_logger.hpp"

namespace hardpoll {

struct NodeConfig {
    std::string node_id = "node-0";
    std::string trace_path = "traces/node-0.jsonl";
    SensorConfig sensor;
    AdaptivePollerConfig poller;
    LatencyInjectorConfig injector;
};

// A simulation node: sensor → adaptive poller → latency injector → trace log.
// In a real system the last stage would publish to a downstream bus; here we
// record the full timeline so the Python analyzer can quantify latency/jitter.
class Node {
public:
    explicit Node(NodeConfig cfg);
    ~Node();

    bool start();
    void stop();
    void wait_for(std::uint64_t events);

    // Deliver a simulated hardware interrupt to the poller (only meaningful
    // in Interrupt mode; a no-op otherwise).
    void notify_interrupt();

    const NodeConfig& config() const { return cfg_; }
    std::uint64_t events_published() const { return published_.load(); }

private:
    void on_event(std::uint64_t sequence, std::uint64_t t_produce_ns,
                  std::uint64_t t_detect_ns, std::uint64_t iterations);

    NodeConfig cfg_;
    SensorSlot slot_{};
    TraceLogger logger_;
    std::unique_ptr<Sensor> sensor_;
    std::unique_ptr<AdaptivePoller> poller_;
    std::unique_ptr<LatencyInjector> injector_;
    std::atomic<std::uint64_t> published_{0};
};

}  // namespace hardpoll
