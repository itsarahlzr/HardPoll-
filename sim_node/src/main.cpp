// hardpoll_sim_node: standalone simulation node.
//
// Typical usage (driven by the Python orchestrator):
//   hardpoll_sim_node --node-id=n0 --mode=adaptive --rate=10000
//                     --duration-ms=1000 --trace=traces/n0.jsonl
//
// Or in server mode, waiting for the orchestrator:
//   hardpoll_sim_node --server --port=0 --node-id=n0 --trace=traces/n0.jsonl

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "hardpoll/control_server.hpp"
#include "hardpoll/node.hpp"

namespace {

bool parse_uint(const std::string& s, std::uint64_t& out) {
    try {
        out = std::stoull(s);
        return true;
    } catch (...) {
        return false;
    }
}

struct Args {
    hardpoll::NodeConfig node;
    bool server = false;
    std::string host = "127.0.0.1";
    int port = 0;
    std::uint64_t duration_ms = 1000;
    std::uint64_t max_events = 0;
};

void print_usage() {
    std::cout <<
        "Usage: hardpoll_sim_node [options]\n"
        "  --node-id=STR         logical id\n"
        "  --trace=PATH          JSONL trace output\n"
        "  --mode=MODE           interrupt|software|fpga|adaptive\n"
        "  --rate=HZ             sensor rate in Hz (0 = as fast as possible)\n"
        "  --jitter=FRAC         sensor jitter fraction (0..1)\n"
        "  --max-events=N        stop after N events\n"
        "  --duration-ms=N       run duration in ms (ignored if --server)\n"
        "  --pin-cpu=N           CPU to pin the polling thread\n"
        "  --backoff-ns=N        adaptive backoff starting sleep (ns)\n"
        "  --max-backoff-ns=N    adaptive backoff upper bound (ns)\n"
        "  --idle-iters=N        empty iters before backoff in adaptive mode\n"
        "  --interrupt-wake-ns=N simulated IRQ wake latency for interrupt mode\n"
        "  --base-latency-ns=N   deterministic injector latency (ns)\n"
        "  --jitter-ns=N         injector jitter bound (ns)\n"
        "  --server              run as TCP control server\n"
        "  --host=IP             server bind host\n"
        "  --port=N              server bind port (0 = ephemeral)\n";
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto eq = s.find('=');
        std::string k = eq == std::string::npos ? s : s.substr(0, eq);
        std::string v = eq == std::string::npos ? "" : s.substr(eq + 1);

        if (k == "--help" || k == "-h") { print_usage(); std::exit(0); }
        else if (k == "--server") a.server = true;
        else if (k == "--node-id") a.node.node_id = v;
        else if (k == "--trace") a.node.trace_path = v;
        else if (k == "--mode") a.node.poller.mode = hardpoll::parse_poll_mode(v);
        else if (k == "--rate") a.node.sensor.rate_hz = std::stod(v);
        else if (k == "--jitter") a.node.sensor.jitter_fraction = std::stod(v);
        else if (k == "--max-events") parse_uint(v, a.node.sensor.max_events);
        else if (k == "--duration-ms") parse_uint(v, a.duration_ms);
        else if (k == "--pin-cpu") a.node.poller.pin_cpu = std::stoi(v);
        else if (k == "--backoff-ns")
            parse_uint(v, a.node.poller.backoff_sleep_ns);
        else if (k == "--max-backoff-ns")
            parse_uint(v, a.node.poller.max_backoff_sleep_ns);
        else if (k == "--idle-iters")
            parse_uint(v, a.node.poller.idle_iterations_before_backoff);
        else if (k == "--interrupt-wake-ns")
            parse_uint(v, a.node.poller.interrupt_wake_ns);
        else if (k == "--base-latency-ns")
            parse_uint(v, a.node.injector.base_ns);
        else if (k == "--jitter-ns")
            parse_uint(v, a.node.injector.jitter_ns);
        else if (k == "--host") a.host = v;
        else if (k == "--port") a.port = std::stoi(v);
        else {
            std::cerr << "unknown option: " << k << "\n";
            print_usage();
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) return 2;

    hardpoll::Node node(a.node);

    if (a.server) {
        hardpoll::ControlServer srv(&node, a.host, a.port);
        if (!srv.start()) {
            std::cerr << "failed to start control server\n";
            return 1;
        }
        std::cout << "{\"event\":\"listening\",\"host\":\"" << a.host
                  << "\",\"port\":" << srv.bound_port() << ",\"node_id\":\""
                  << a.node.node_id << "\"}" << std::endl;
        srv.wait();
        return 0;
    }

    if (!node.start()) {
        std::cerr << "failed to start node\n";
        return 1;
    }
    if (a.node.sensor.max_events > 0) {
        node.wait_for(a.node.sensor.max_events);
    } else {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(a.duration_ms));
    }
    node.stop();
    std::cout << "{\"event\":\"done\",\"node_id\":\"" << a.node.node_id
              << "\",\"published\":" << node.events_published() << "}\n";
    return 0;
}
