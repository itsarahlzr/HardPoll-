#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace hardpoll {

// One trace record per event moving through the node pipeline.
// All timestamps are monotonic nanoseconds from the node's steady_clock.
struct TraceRecord {
    std::string node_id;
    std::uint64_t event_id = 0;
    std::uint64_t t_produce_ns = 0;   // sensor generated the sample
    std::uint64_t t_detect_ns = 0;    // poller observed the sample
    std::uint64_t t_inject_ns = 0;    // latency injector finished delaying
    std::uint64_t t_publish_ns = 0;   // downstream notified
    std::string poll_mode;            // "interrupt" | "software" | "fpga" | "adaptive"
    std::uint64_t poll_iterations = 0;
    std::uint64_t injected_latency_ns = 0;
};

class TraceLogger {
public:
    TraceLogger();
    ~TraceLogger();

    // Open a JSON-lines file. Directory must already exist.
    bool open(const std::string& path);
    void close();

    // Thread-safe append.
    void log(const TraceRecord& rec);

    bool is_open() const;

private:
    mutable std::mutex mu_;
    std::ofstream out_;
};

}  // namespace hardpoll
