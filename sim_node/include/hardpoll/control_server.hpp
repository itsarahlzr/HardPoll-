#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "hardpoll/node.hpp"

namespace hardpoll {

// Minimal line-delimited JSON control server used by the Python orchestrator
// to drive a sim_node. Supported commands:
//   {"cmd":"status"}
//   {"cmd":"start"}
//   {"cmd":"stop"}
//   {"cmd":"interrupt"}     // deliver a simulated IRQ (Interrupt mode only)
//   {"cmd":"shutdown"}
class ControlServer {
public:
    ControlServer(Node* node, std::string bind_host, int port);
    ~ControlServer();

    bool start();
    void stop();
    void wait();

    int bound_port() const { return port_; }

private:
    void run();
    std::string handle_line(const std::string& line);

    Node* node_;
    std::string host_;
    int port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::thread thread_;
};

}  // namespace hardpoll
