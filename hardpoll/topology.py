"""Topology and access-pattern descriptions used by the UI and orchestrator.

A :class:`Topology` is a small, YAML-friendly data model that captures the
structure of the simulated distributed system (nodes, links, access
patterns, polling models). The orchestrator consumes a topology and spawns
the corresponding sim_node processes.
"""

from __future__ import annotations

import dataclasses
import enum
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional


class PollMode(str, enum.Enum):
    """Polling strategy applied on a simulation node."""

    INTERRUPT = "interrupt"
    SOFTWARE = "software"
    FPGA = "fpga"
    ADAPTIVE = "adaptive"


class AccessPattern(str, enum.Enum):
    """High-level description of how data is produced/consumed across nodes."""

    STEADY = "steady"        # constant rate per node
    BURST = "burst"          # periodic bursts
    POISSON = "poisson"      # exponential inter-arrival times
    CORRELATED = "correlated"  # synchronized across nodes


@dataclass
class SensorSpec:
    rate_hz: float = 1000.0
    jitter_fraction: float = 0.0
    max_events: int = 0  # 0 = unbounded


@dataclass
class PollerSpec:
    mode: PollMode = PollMode.ADAPTIVE
    idle_iterations_before_backoff: int = 1024
    backoff_sleep_ns: int = 2_000
    max_backoff_sleep_ns: int = 100_000
    pin_cpu: int = -1
    interrupt_wake_ns: int = 8_000


@dataclass
class InjectorSpec:
    base_ns: int = 500
    jitter_ns: int = 50
    emulate_fpga: bool = True


@dataclass
class NodeSpec:
    node_id: str
    sensor: SensorSpec = field(default_factory=SensorSpec)
    poller: PollerSpec = field(default_factory=PollerSpec)
    injector: InjectorSpec = field(default_factory=InjectorSpec)
    host: str = "127.0.0.1"

    def cli_args(self, trace_path: Path, duration_ms: int) -> List[str]:
        """CLI flags to spawn this node via ``hardpoll_sim_node``."""
        args = [
            f"--node-id={self.node_id}",
            f"--trace={trace_path}",
            f"--mode={self.poller.mode.value}",
            f"--rate={self.sensor.rate_hz}",
            f"--jitter={self.sensor.jitter_fraction}",
            f"--max-events={self.sensor.max_events}",
            f"--duration-ms={duration_ms}",
            f"--pin-cpu={self.poller.pin_cpu}",
            f"--backoff-ns={self.poller.backoff_sleep_ns}",
            f"--idle-iters={self.poller.idle_iterations_before_backoff}",
            f"--base-latency-ns={self.injector.base_ns}",
            f"--jitter-ns={self.injector.jitter_ns}",
        ]
        return args


@dataclass
class Topology:
    """Container for a set of nodes that will be simulated together."""

    name: str = "unnamed"
    access_pattern: AccessPattern = AccessPattern.STEADY
    nodes: List[NodeSpec] = field(default_factory=list)
    description: str = ""

    def with_node(self, spec: NodeSpec) -> "Topology":
        self.nodes.append(spec)
        return self

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Topology":
        nodes: List[NodeSpec] = []
        for n in data.get("nodes", []):
            nodes.append(
                NodeSpec(
                    node_id=n["node_id"],
                    host=n.get("host", "127.0.0.1"),
                    sensor=SensorSpec(**n.get("sensor", {})),
                    poller=_poller_from_dict(n.get("poller", {})),
                    injector=InjectorSpec(**n.get("injector", {})),
                )
            )
        return cls(
            name=data.get("name", "unnamed"),
            access_pattern=AccessPattern(data.get("access_pattern", "steady")),
            description=data.get("description", ""),
            nodes=nodes,
        )

    @classmethod
    def from_yaml(cls, path: str | Path) -> "Topology":
        import yaml

        with open(path, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
        return cls.from_dict(data)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "description": self.description,
            "access_pattern": self.access_pattern.value,
            "nodes": [_node_to_dict(n) for n in self.nodes],
        }


def _poller_from_dict(d: Dict[str, Any]) -> PollerSpec:
    if "mode" in d:
        d = {**d, "mode": PollMode(d["mode"])}
    return PollerSpec(**d)


def _node_to_dict(n: NodeSpec) -> Dict[str, Any]:
    out = {
        "node_id": n.node_id,
        "host": n.host,
        "sensor": dataclasses.asdict(n.sensor),
        "poller": {**dataclasses.asdict(n.poller), "mode": n.poller.mode.value},
        "injector": dataclasses.asdict(n.injector),
    }
    return out


def expand_access_pattern(
    topo: Topology, overrides: Optional[Dict[str, Any]] = None
) -> Topology:
    """Apply the access pattern to each node's sensor config.

    Interpreting the access pattern here keeps the C++ runtime focused on
    deterministic event generation; the orchestrator layer is where
    scenario-shaped distributions are translated into node parameters.
    """

    overrides = overrides or {}
    for node in topo.nodes:
        if topo.access_pattern is AccessPattern.STEADY:
            node.sensor.jitter_fraction = 0.0
        elif topo.access_pattern is AccessPattern.BURST:
            node.sensor.jitter_fraction = max(
                node.sensor.jitter_fraction, 0.5
            )
        elif topo.access_pattern is AccessPattern.POISSON:
            node.sensor.jitter_fraction = 1.0
        elif topo.access_pattern is AccessPattern.CORRELATED:
            node.sensor.jitter_fraction = 0.1
        for k, v in overrides.items():
            if hasattr(node.sensor, k):
                setattr(node.sensor, k, v)
    return topo
