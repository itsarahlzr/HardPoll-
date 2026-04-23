#include "hardpoll/node.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "hardpoll/time_utils.hpp"

namespace hardpoll {

Node::Node(NodeConfig cfg) : cfg_(std::move(cfg)) {}

Node::~Node() { stop(); }

bool Node::start() {
    if (!cfg_.trace_path.empty()) {
        std::filesystem::path p(cfg_.trace_path);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        if (!logger_.open(cfg_.trace_path)) {
            std::cerr << "[node " << cfg_.node_id
                      << "] failed to open trace file: " << cfg_.trace_path
                      << std::endl;
            return false;
        }
    }

    injector_ = std::make_unique<LatencyInjector>(cfg_.injector);
    poller_ = std::make_unique<AdaptivePoller>(
        &slot_, cfg_.poller,
        [this](std::uint64_t seq, std::uint64_t t_prod, std::uint64_t t_det,
               std::uint64_t iters) {
            on_event(seq, t_prod, t_det, iters);
        });
    sensor_ = std::make_unique<Sensor>(&slot_, cfg_.sensor);
    if (cfg_.poller.mode == PollMode::Interrupt) {
        AdaptivePoller* p = poller_.get();
        sensor_->set_interrupt_notifier([p] { p->notify_interrupt(); });
    }

    poller_->start();
    sensor_->start();
    return true;
}

void Node::stop() {
    if (sensor_) sensor_->stop();
    if (poller_) poller_->stop();
    logger_.close();
}

void Node::wait_for(std::uint64_t events) {
    while (published_.load(std::memory_order_relaxed) < events) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Node::on_event(std::uint64_t sequence, std::uint64_t t_produce_ns,
                    std::uint64_t t_detect_ns, std::uint64_t iterations) {
    TraceRecord rec;
    rec.node_id = cfg_.node_id;
    rec.event_id = sequence;
    rec.t_produce_ns = t_produce_ns;
    rec.t_detect_ns = t_detect_ns;
    rec.poll_mode = poll_mode_name(cfg_.poller.mode);
    rec.poll_iterations = iterations;

    const auto injected = injector_->inject();
    rec.t_inject_ns = now_ns();
    rec.injected_latency_ns = injected;
    rec.t_publish_ns = now_ns();
    logger_.log(rec);
    published_.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace hardpoll
