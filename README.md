# HardPoll

**HardPoll** is a simulation framework for studying data-distribution latency
when the interrupt-driven delivery model is replaced by **adaptive polling** —
either hardware-accelerated (FPGA / smart-NIC) or software-based
(busy-polling on dedicated cores). The project targets the microsecond-jitter
regime and provides the plumbing to run, trace and analyze side-by-side
experiments.

The design follows the layered architecture below (cf. the project spec):

```
┌─────────────────────────────────────────────────────────────┐
│  Interface utilisateur                                       │   Python
│  (descriptions de topologie, patterns d'accès, modèles)      │   hardpoll.ui
├─────────────────────────────────────────────────────────────┤
│  Orchestrateur logiciel                                      │   Python
│  (découpe des simulations, gestion des scénarios)            │   hardpoll.orchestrator
├─────────────────────────────────────────────────────────────┤
│  Nœud simu × N                                               │   C++ (hardpoll_sim_node)
│    • OS modifié + capteurs  (Sensor / AdaptivePoller)        │
│    • FPGA/board injecteur de latence (LatencyInjector)       │
├─────────────────────────────────────────────────────────────┤
│  Collecteur de traces & analyse                              │   Python
│  (latence P50/P99/P999, jitter)                              │   hardpoll.collector + analyze
└─────────────────────────────────────────────────────────────┘
```

## Polling modes

Every simulation node instantiates exactly one poller. The four modes are
deliberately comparable so you can quantify the improvement brought by
adaptive polling over the interrupt baseline.

| Mode        | Description                                                                                  |
|-------------|----------------------------------------------------------------------------------------------|
| `interrupt` | Baseline. Poller is parked until a "hardware IRQ" fires, plus a configurable wake latency.   |
| `software`  | Busy-poll on a pinned core; uses `pause` / `yield` between reads.                            |
| `fpga`      | Models a smart-NIC / FPGA polling a DMA descriptor — a tight loop with no scheduling points. |
| `adaptive`  | Starts in busy-poll, backs off to exponential short sleeps when idle, resumes on traffic.    |

The shared memory between sensor and poller is a single-writer /
single-reader `std::atomic<uint64_t>` slot, which is the closest software
equivalent of a DMA doorbell.

## Layout

```
hardpoll/           # Python package (UI, orchestrator, collector, analysis)
sim_node/           # C++ simulation node (CMake project)
  include/hardpoll/
  src/
  tests/
examples/           # YAML topology definitions
tests/              # pytest suite
```

## Build & run

### Prerequisites

- CMake ≥ 3.16, a C++17 compiler, pthreads
- Python ≥ 3.9, `pip install -e .[dev]` (pulls `PyYAML`, `pytest`)

### Build the C++ simulator

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# or just:  make build
```

This produces:

- `build/sim_node/hardpoll_sim_node` — the per-node simulation runtime
- `build/sim_node/hardpoll_tests` — C++ self-tests (`ctest --test-dir build`)

### Run a demo scenario

```bash
make run-demo
```

…which is equivalent to:

```bash
python3 -m hardpoll run --preset comparison --duration-ms 500 \
    --rate-hz 5000 --analyze
```

Example output on a non-tuned box:

```
node            mode       count     p50(us)   p99(us)   p999(us)  max(us)
node-adaptive   adaptive   100       76.74     266.68    274.08    274.08
node-fpga       fpga       336       0.78      230.97    1425.39   1425.39
node-interrupt  interrupt  53        163.86    4162.93   4163.87   4163.87
node-software   software   331       0.76      3901.42   8398.19   8398.19
```

On a real tuned host (isolated cores, CPU pinning) the software/FPGA p50 and
p99 shrink to a few microseconds, which is the point of the adaptive-polling
design.

### Run from a topology file

```bash
python3 -m hardpoll run --topology examples/comparison.yaml \
    --name cmp --duration-ms 1000 --analyze
```

### Analyze an existing run

```bash
python3 -m hardpoll analyze runs/cmp/rep-000 --json
```

## Python API

```python
from hardpoll import Orchestrator, Scenario
from hardpoll.ui import homogeneous_topology
from hardpoll.topology import PollMode
from hardpoll.analyze import analyze_traces, format_report

topo = homogeneous_topology(
    "my-run", num_nodes=4, mode=PollMode.ADAPTIVE, rate_hz=50_000.0,
)
result = Orchestrator().run(
    Scenario(name="my-run", topology=topo, duration_ms=2_000)
)
print(format_report(analyze_traces(result.run_dir)))
```

## Standalone C++ usage

The node binary can also be driven directly (useful for profiling or
hooking into another orchestrator):

```bash
./build/sim_node/hardpoll_sim_node \
    --node-id=n0 --mode=adaptive --rate=50000 \
    --duration-ms=1000 --trace=traces/n0.jsonl --pin-cpu=3
```

Flags are documented via `--help`. A line-delimited JSON control server is
also available (`--server --port=0`) for orchestrators that want to
start/stop nodes out-of-band.

## Trace format

Each node writes one JSON object per event to the trace file:

```json
{
  "node_id": "node-adaptive",
  "event_id": 42,
  "t_produce_ns": 123456789,
  "t_detect_ns":  123457001,
  "t_inject_ns":  123457555,
  "t_publish_ns": 123457556,
  "poll_mode": "adaptive",
  "poll_iterations": 37,
  "injected_latency_ns": 512
}
```

Timestamps use a monotonic `steady_clock` per node. `t_publish_ns -
t_produce_ns` is the end-to-end figure reported in the analysis table.

## Testing

```bash
ctest --test-dir build           # C++ self-tests
pytest                           # Python unit tests (skip e2e if binary missing)
```

The Python suite covers the UI, topology round-tripping, trace parsing and
statistics without requiring the C++ binary; the `test_end_to_end.py` file
additionally validates a full orchestrated run when the binary is present.

## License

See [LICENSE](./LICENSE).
