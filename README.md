# HardPoll

**Microsecond-latency data distribution through adaptive polling.**

HardPoll replaces the traditional interrupt-driven model for data delivery with an **adaptive polling** pipeline — either **hardware-accelerated** (FPGA / smart NIC) or **software-based** (busy-polling on dedicated CPU cores) — to deliver data with **microsecond-scale latency** and **minimal jitter**.

---

## Motivation

Interrupt-based I/O was designed for fairness and CPU efficiency, not for latency. Every interrupt incurs:

- context switches and cache pollution,
- scheduler wake-up overhead,
- unpredictable tail latency driven by OS noise.

For workloads such as **high-frequency trading**, **real-time market data distribution**, **low-latency telemetry**, and **in-network computing**, these costs are unacceptable. HardPoll bypasses them by keeping a reader *always ready* — either in silicon or on a pinned core — so data is observed the instant it is available.

## Key Ideas

- **Adaptive polling.** The polling rate adjusts to load: tight spin loops under pressure, relaxed polling under idle conditions, preserving both latency and energy efficiency.
- **Hardware path (FPGA / NIC).** Packet arrival and buffer watermarks are polled directly on the NIC or an FPGA accelerator, bypassing the kernel network stack entirely.
- **Software path (busy-poll).** Dedicated, isolated CPU cores run a user-space busy-polling loop (kernel-bypass via DPDK / AF_XDP / io_uring-style primitives) with no interrupts, no syscalls on the hot path.
- **Jitter minimization.** Core isolation (`isolcpus`, `nohz_full`), IRQ affinity, huge pages, and cache-line-aligned data structures keep the P99 close to the P50.

## Architecture

```
         ┌───────────────────────────────┐
         │      Producer / Exchange      │
         └──────────────┬────────────────┘
                        │  (shared mem / NIC RX ring)
                        ▼
    ┌───────────────────────────────────────────┐
    │              HardPoll Engine              │
    │                                           │
    │   ┌────────────┐       ┌──────────────┐   │
    │   │ HW Poller  │  or   │  SW Poller   │   │
    │   │ FPGA / NIC │       │ Pinned Core  │   │
    │   └─────┬──────┘       └──────┬───────┘   │
    │         └──────────┬──────────┘           │
    │                    ▼                      │
    │           Adaptive Rate Controller        │
    └────────────────────┬──────────────────────┘
                         ▼
              Consumers (lock-free queues)
```

## Targets

| Metric            | Goal                          |
| ----------------- | ----------------------------- |
| Median latency    | < 5 µs end-to-end             |
| P99 latency       | < 20 µs                       |
| Jitter (stddev)   | < 2 µs                        |
| Throughput        | line-rate on 10/25/100 GbE    |

## Status

Early-stage research project. The repository currently contains the design; implementation and benchmarks are in progress.

## Roadmap

- [ ] Software busy-polling prototype (DPDK / AF_XDP)
- [ ] Adaptive polling controller (load-aware spin/backoff)
- [ ] FPGA / smart-NIC offload path
- [ ] End-to-end latency & jitter benchmarks vs. interrupt baseline
- [ ] Publication of reproducible results

## License

Released under the [MIT License](LICENSE).
