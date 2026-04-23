"""Unit tests for the Python UI/topology/analysis layer (no C++ needed)."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from hardpoll.analyze import analyze_traces, format_report, summarize
from hardpoll.collector import TraceCollector
from hardpoll.topology import AccessPattern, PollMode, Topology
from hardpoll.ui import comparison_topology, homogeneous_topology


def test_homogeneous_topology_matches_request() -> None:
    topo = homogeneous_topology(
        "sw",
        num_nodes=3,
        mode=PollMode.SOFTWARE,
        rate_hz=2500.0,
        access_pattern=AccessPattern.BURST,
    )
    assert len(topo.nodes) == 3
    assert all(n.poller.mode is PollMode.SOFTWARE for n in topo.nodes)
    assert all(n.sensor.rate_hz == 2500.0 for n in topo.nodes)
    assert topo.access_pattern is AccessPattern.BURST


def test_comparison_topology_has_each_mode() -> None:
    topo = comparison_topology()
    modes = {n.poller.mode for n in topo.nodes}
    assert modes == set(PollMode)


def test_topology_round_trip_via_dict() -> None:
    topo = homogeneous_topology("rt", num_nodes=2)
    data = topo.to_dict()
    copy = Topology.from_dict(data)
    assert copy.name == topo.name
    assert len(copy.nodes) == len(topo.nodes)
    assert copy.nodes[0].poller.mode is topo.nodes[0].poller.mode


def test_cli_args_include_mode_and_rate(tmp_path: Path) -> None:
    topo = homogeneous_topology("cli", num_nodes=1, mode=PollMode.FPGA)
    node = topo.nodes[0]
    args = node.cli_args(tmp_path / "t.jsonl", duration_ms=250)
    assert "--mode=fpga" in args
    assert "--duration-ms=250" in args


def test_summarize_handles_empty_and_values() -> None:
    empty = summarize([])
    assert empty.count == 0
    stats = summarize([100, 200, 300, 400, 500])
    assert stats.count == 5
    assert stats.min_ns == 100
    assert stats.max_ns == 500
    assert stats.median_ns == 300
    assert stats.p99_ns >= stats.median_ns


def test_trace_collector_reads_jsonl(tmp_path: Path) -> None:
    traces_dir = tmp_path / "traces"
    traces_dir.mkdir()
    records = [
        {
            "node_id": "n0",
            "event_id": i + 1,
            "t_produce_ns": 1_000 * i,
            "t_detect_ns": 1_000 * i + 100,
            "t_inject_ns": 1_000 * i + 600,
            "t_publish_ns": 1_000 * i + 650,
            "poll_mode": "adaptive",
            "poll_iterations": 42,
            "injected_latency_ns": 500,
        }
        for i in range(10)
    ]
    with open(traces_dir / "n0.jsonl", "w", encoding="utf-8") as f:
        for r in records:
            f.write(json.dumps(r) + "\n")

    reports = analyze_traces(tmp_path)
    assert len(reports) == 1
    report = reports[0]
    assert report.node_id == "n0"
    assert report.poll_mode == "adaptive"
    assert report.end_to_end.count == 10
    text = format_report(reports)
    assert "adaptive" in text
    assert "n0" in text


def test_expand_access_pattern_sets_jitter() -> None:
    topo = homogeneous_topology(
        "poisson",
        num_nodes=2,
        access_pattern=AccessPattern.POISSON,
    )
    # expand is applied via orchestrator.run; call it directly to verify.
    from hardpoll.topology import expand_access_pattern

    expand_access_pattern(topo)
    assert all(n.sensor.jitter_fraction == 1.0 for n in topo.nodes)
