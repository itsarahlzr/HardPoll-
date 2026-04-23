"""HardPoll: adaptive-polling simulation framework.

Python side: UI, orchestrator, trace collector and analyzer. The per-node
simulation runtime lives in ``sim_node/`` and is implemented in C++ for
deterministic timing.
"""

from .topology import AccessPattern, NodeSpec, Topology
from .scenario import Scenario, ScenarioResult
from .orchestrator import Orchestrator
from .collector import Trace, TraceCollector
from .analyze import LatencyStats, analyze_traces

__all__ = [
    "AccessPattern",
    "NodeSpec",
    "Topology",
    "Scenario",
    "ScenarioResult",
    "Orchestrator",
    "Trace",
    "TraceCollector",
    "LatencyStats",
    "analyze_traces",
]

__version__ = "0.1.0"
