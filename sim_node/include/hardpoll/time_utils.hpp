#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace hardpoll {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

inline std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
}

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause" ::: "memory");
#elif defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}

inline void busy_wait_ns(std::uint64_t ns) {
    const auto deadline = Clock::now() + std::chrono::nanoseconds(ns);
    while (Clock::now() < deadline) {
        cpu_relax();
    }
}

}  // namespace hardpoll
