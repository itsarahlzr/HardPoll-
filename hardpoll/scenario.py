"""Scenario definitions and results.

A scenario pairs a :class:`Topology` with runtime parameters (duration,
repetitions, output directory). The orchestrator splits the simulation per
node, runs it, and returns a :class:`ScenarioResult`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from .topology import Topology


@dataclass
class Scenario:
    name: str
    topology: Topology
    duration_ms: int = 1000
    repetitions: int = 1
    output_dir: Path = Path("runs")

    def run_dir(self, repetition: int) -> Path:
        return self.output_dir / self.name / f"rep-{repetition:03d}"


@dataclass
class NodeRunResult:
    node_id: str
    trace_path: Path
    exit_code: int
    stdout: str = ""
    stderr: str = ""
    published: Optional[int] = None


@dataclass
class ScenarioResult:
    scenario_name: str
    run_dir: Path
    nodes: List[NodeRunResult] = field(default_factory=list)
    succeeded: bool = True

    def by_node(self) -> Dict[str, NodeRunResult]:
        return {n.node_id: n for n in self.nodes}
