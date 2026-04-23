#include "hardpoll/trace_logger.hpp"

#include <sstream>

namespace hardpoll {

TraceLogger::TraceLogger() = default;

TraceLogger::~TraceLogger() { close(); }

bool TraceLogger::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);
    out_.open(path, std::ios::out | std::ios::trunc);
    return static_cast<bool>(out_);
}

void TraceLogger::close() {
    std::lock_guard<std::mutex> lock(mu_);
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

bool TraceLogger::is_open() const {
    std::lock_guard<std::mutex> lock(mu_);
    return out_.is_open();
}

void TraceLogger::log(const TraceRecord& rec) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!out_.is_open()) return;
    // Hand-rolled JSON to avoid a heavy dependency. The fields are all safe
    // characters (ASCII ids, integers) so no escaping is required here.
    out_ << "{"
         << "\"node_id\":\"" << rec.node_id << "\","
         << "\"event_id\":" << rec.event_id << ","
         << "\"t_produce_ns\":" << rec.t_produce_ns << ","
         << "\"t_detect_ns\":" << rec.t_detect_ns << ","
         << "\"t_inject_ns\":" << rec.t_inject_ns << ","
         << "\"t_publish_ns\":" << rec.t_publish_ns << ","
         << "\"poll_mode\":\"" << rec.poll_mode << "\","
         << "\"poll_iterations\":" << rec.poll_iterations << ","
         << "\"injected_latency_ns\":" << rec.injected_latency_ns
         << "}\n";
}

}  // namespace hardpoll
