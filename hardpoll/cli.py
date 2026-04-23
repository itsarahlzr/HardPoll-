"""Command-line entry point: ``python -m hardpoll`` / ``hardpoll``."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import List

from .analyze import analyze_traces, format_report
from .orchestrator import Orchestrator
from .scenario import Scenario
from .topology import AccessPattern, PollMode
from .ui import comparison_topology, homogeneous_topology, load_topology


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="hardpoll",
        description=(
            "HardPoll: adaptive-polling distributed-latency simulator"
        ),
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    run = sub.add_parser("run", help="run a scenario")
    run.add_argument("--topology", type=Path, help="YAML topology file")
    run.add_argument("--name", default="scenario", help="scenario name")
    run.add_argument("--duration-ms", type=int, default=1000)
    run.add_argument("--repetitions", type=int, default=1)
    run.add_argument("--output-dir", type=Path, default=Path("runs"))
    run.add_argument(
        "--preset",
        choices=["comparison", "homogeneous"],
        help="use a built-in topology preset instead of --topology",
    )
    run.add_argument("--nodes", type=int, default=4, help="preset node count")
    run.add_argument(
        "--mode",
        choices=[m.value for m in PollMode],
        default=PollMode.ADAPTIVE.value,
        help="polling mode for the 'homogeneous' preset",
    )
    run.add_argument(
        "--rate-hz", type=float, default=10_000.0,
        help="sensor rate for preset topologies",
    )
    run.add_argument(
        "--access-pattern",
        choices=[a.value for a in AccessPattern],
        default=AccessPattern.STEADY.value,
    )
    run.add_argument("--binary", type=Path, help="path to hardpoll_sim_node")
    run.add_argument(
        "--no-parallel", action="store_true",
        help="run nodes sequentially (useful for debugging)",
    )
    run.add_argument(
        "--analyze", action="store_true",
        help="analyze traces after the run",
    )

    rep = sub.add_parser("analyze", help="analyze traces from a run directory")
    rep.add_argument("run_dir", type=Path)
    rep.add_argument("--json", action="store_true")

    sub.add_parser("topologies", help="list built-in topology presets")

    return p


def _topology_for_run(args: argparse.Namespace):
    if args.topology:
        return load_topology(args.topology)
    preset = args.preset or "comparison"
    if preset == "comparison":
        return comparison_topology(name=args.name, rate_hz=args.rate_hz)
    if preset == "homogeneous":
        return homogeneous_topology(
            name=args.name,
            num_nodes=args.nodes,
            mode=PollMode(args.mode),
            rate_hz=args.rate_hz,
            access_pattern=AccessPattern(args.access_pattern),
        )
    raise SystemExit(f"unknown preset: {preset}")


def main(argv: List[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.cmd == "run":
        topo = _topology_for_run(args)
        scenario = Scenario(
            name=args.name,
            topology=topo,
            duration_ms=args.duration_ms,
            repetitions=args.repetitions,
            output_dir=args.output_dir,
        )
        orch = Orchestrator(
            sim_node_binary=args.binary,
            parallel=not args.no_parallel,
        )
        result = orch.run(scenario)
        print(f"run_dir: {result.run_dir}")
        for n in result.nodes:
            status = "OK" if n.exit_code == 0 else f"FAIL({n.exit_code})"
            print(
                f"  {n.node_id}: {status} published={n.published} "
                f"trace={n.trace_path}"
            )
        if args.analyze:
            reports = analyze_traces(result.run_dir)
            print()
            print(format_report(reports))
        return 0 if result.succeeded else 1

    if args.cmd == "analyze":
        reports = analyze_traces(args.run_dir)
        if args.json:
            print(json.dumps([r.as_dict() for r in reports], indent=2))
        else:
            print(format_report(reports))
        return 0

    if args.cmd == "topologies":
        print("comparison   - one node per polling mode")
        print("homogeneous  - N identical nodes (pick --mode)")
        return 0

    return 2


if __name__ == "__main__":
    sys.exit(main())
