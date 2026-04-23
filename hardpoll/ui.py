"""User interface helpers: build topologies from dicts / YAML / CLI flags.

This is the "Interface utilisateur" layer of the diagram: topology
descriptions, access patterns and models turned into :class:`Topology`
instances suitable for the orchestrator.
"""

from __future__ import annotations

from pathlib import Path
from typing import Iterable, Optional

from .topology import (
    AccessPattern,
    InjectorSpec,
    NodeSpec,
    PollMode,
    PollerSpec,
    SensorSpec,
    Topology,
)


def homogeneous_topology(
    name: str,
    num_nodes: int,
    mode: PollMode = PollMode.ADAPTIVE,
    rate_hz: float = 10_000.0,
    access_pattern: AccessPattern = AccessPattern.STEADY,
    base_latency_ns: int = 500,
    jitter_ns: int = 50,
    pin_cpu_offset: Optional[int] = None,
) -> Topology:
    """Build a topology with ``num_nodes`` identical nodes.

    ``pin_cpu_offset`` pins node *i* to CPU ``offset + i``; useful for
    realistic busy-polling tests when you have enough cores available.
    """

    nodes = []
    for i in range(num_nodes):
        pin = -1 if pin_cpu_offset is None else pin_cpu_offset + i
        nodes.append(
            NodeSpec(
                node_id=f"node-{i:02d}",
                sensor=SensorSpec(rate_hz=rate_hz),
                poller=PollerSpec(mode=mode, pin_cpu=pin),
                injector=InjectorSpec(
                    base_ns=base_latency_ns, jitter_ns=jitter_ns
                ),
            )
        )
    return Topology(
        name=name,
        access_pattern=access_pattern,
        nodes=nodes,
        description=(
            f"Homogeneous {num_nodes}-node topology, {mode.value} polling, "
            f"{rate_hz:.0f} Hz, pattern={access_pattern.value}"
        ),
    )


def comparison_topology(
    name: str = "comparison",
    rate_hz: float = 10_000.0,
) -> Topology:
    """One node per polling mode, useful for side-by-side latency plots."""

    modes = [
        PollMode.INTERRUPT,
        PollMode.SOFTWARE,
        PollMode.FPGA,
        PollMode.ADAPTIVE,
    ]
    nodes = [
        NodeSpec(
            node_id=f"node-{m.value}",
            sensor=SensorSpec(rate_hz=rate_hz),
            poller=PollerSpec(mode=m),
        )
        for m in modes
    ]
    return Topology(
        name=name,
        access_pattern=AccessPattern.STEADY,
        nodes=nodes,
        description="One node per polling mode for A/B comparison.",
    )


def load_topology(path: Path | str) -> Topology:
    p = Path(path)
    return Topology.from_yaml(p)
