"""End-to-end tests: requires the C++ binary to be built.

These tests are skipped automatically when ``hardpoll_sim_node`` is not
available (e.g. CI environments that only run the Python layer).
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from hardpoll.analyze import analyze_traces
from hardpoll.orchestrator import Orchestrator, default_sim_node_binary
from hardpoll.scenario import Scenario
from hardpoll.topology import PollMode
from hardpoll.ui import comparison_topology, homogeneous_topology


def _binary_available() -> bool:
    return default_sim_node_binary().exists()


pytestmark = pytest.mark.skipif(
    not _binary_available(), reason="hardpoll_sim_node binary not built"
)


def test_run_homogeneous_adaptive_scenario(tmp_path: Path) -> None:
    topo = homogeneous_topology(
        "t", num_nodes=2, mode=PollMode.ADAPTIVE, rate_hz=5_000.0
    )
    scenario = Scenario(
        name="adaptive-smoke",
        topology=topo,
        duration_ms=300,
        output_dir=tmp_path,
    )
    result = Orchestrator().run(scenario)
    assert result.succeeded
    assert len(result.nodes) == 2
    for n in result.nodes:
        assert n.trace_path.exists()
        assert n.trace_path.stat().st_size > 0

    reports = analyze_traces(result.run_dir)
    assert len(reports) == 2
    for r in reports:
        assert r.end_to_end.count > 0
        assert r.end_to_end.mean_ns > 0


def test_comparison_scenario_produces_all_modes(tmp_path: Path) -> None:
    topo = comparison_topology(rate_hz=5_000.0)
    result = Orchestrator().run(
        Scenario(
            name="cmp",
            topology=topo,
            duration_ms=250,
            output_dir=tmp_path,
        )
    )
    assert result.succeeded
    reports = {r.node_id: r for r in analyze_traces(result.run_dir)}
    assert {r.poll_mode for r in reports.values()} == {
        "interrupt",
        "software",
        "fpga",
        "adaptive",
    }
