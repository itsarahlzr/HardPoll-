#include "hardpoll/control_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <sstream>

namespace hardpoll {

namespace {

// Tiny helper: find `"key"` and return the integer or string value following
// the colon. Good enough for a fixed command vocabulary.
std::string extract_field(const std::string& msg, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    auto pos = msg.find(token);
    if (pos == std::string::npos) return {};
    pos = msg.find(':', pos);
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < msg.size() && (msg[pos] == ' ' || msg[pos] == '"')) ++pos;
    std::string out;
    while (pos < msg.size() && msg[pos] != '"' && msg[pos] != ',' &&
           msg[pos] != '}' && msg[pos] != ' ') {
        out += msg[pos++];
    }
    return out;
}

std::string send_all(int fd, const std::string& s) {
    ::send(fd, s.data(), s.size(), 0);
    return s;
}

}  // namespace

ControlServer::ControlServer(Node* node, std::string bind_host, int port)
    : node_(node), host_(std::move(bind_host)), port_(port) {}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;
    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (host_.empty() || host_ == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    }
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (port_ == 0) {
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
    }
    if (::listen(listen_fd_, 4) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    running_.store(true);
    thread_ = std::thread([this] { run(); });
    return true;
}

void ControlServer::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

void ControlServer::wait() {
    while (running_.load() && !shutdown_requested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

std::string ControlServer::handle_line(const std::string& line) {
    const auto cmd = extract_field(line, "cmd");
    std::ostringstream out;
    if (cmd == "status") {
        out << "{\"ok\":true,\"node_id\":\"" << node_->config().node_id
            << "\",\"published\":" << node_->events_published()
            << ",\"mode\":\"" << poll_mode_name(node_->config().poller.mode)
            << "\"}\n";
    } else if (cmd == "start") {
        const bool ok = node_->start();
        out << "{\"ok\":" << (ok ? "true" : "false") << "}\n";
    } else if (cmd == "stop") {
        node_->stop();
        out << "{\"ok\":true}\n";
    } else if (cmd == "interrupt") {
        node_->notify_interrupt();
        out << "{\"ok\":true}\n";
    } else if (cmd == "shutdown") {
        node_->stop();
        shutdown_requested_.store(true);
        out << "{\"ok\":true}\n";
    } else {
        out << "{\"ok\":false,\"error\":\"unknown cmd\"}\n";
    }
    return out.str();
}

void ControlServer::run() {
    while (running_.load()) {
        sockaddr_in client{};
        socklen_t len = sizeof(client);
        const int fd =
            ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client), &len);
        if (fd < 0) {
            if (!running_.load()) break;
            continue;
        }
        std::string buf;
        char chunk[512];
        while (running_.load()) {
            const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            buf.append(chunk, chunk + n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                const auto line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                const auto reply = handle_line(line);
                send_all(fd, reply);
                if (shutdown_requested_.load()) break;
            }
            if (shutdown_requested_.load()) break;
        }
        ::close(fd);
        if (shutdown_requested_.load()) {
            running_.store(false);
            break;
        }
    }
}

}  // namespace hardpoll
