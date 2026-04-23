"""Orchestrator: spawns sim_node processes for a scenario.

In the diagram this is the "Orchestrateur logiciel" layer that slices the
simulation across nodes. Each node is a separate C++ process so CPU pinning
and busy-polling stay honest.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import List, Optional

from .scenario import NodeRunResult, Scenario, ScenarioResult
from .topology import NodeSpec, Topology, expand_access_pattern


_DONE_RE = re.compile(r'"published"\s*:\s*(\d+)')


def default_sim_node_binary() -> Path:
    """Locate the ``hardpoll_sim_node`` binary.

    Search order: env var ``HARDPOLL_SIM_NODE``, ``build/sim_node/`` relative
    to the repo root, then ``PATH``.
    """

    env = os.environ.get("HARDPOLL_SIM_NODE")
    if env:
        return Path(env)
    here = Path(__file__).resolve().parent.parent
    candidate = here / "build" / "sim_node" / "hardpoll_sim_node"
    if candidate.exists():
        return candidate
    which = shutil.which("hardpoll_sim_node")
    if which:
        return Path(which)
    # Return the default candidate anyway so callers see a useful error.
    return candidate


class Orchestrator:
    def __init__(
        self,
        sim_node_binary: Optional[Path] = None,
        parallel: bool = True,
    ) -> None:
        self.sim_node_binary = Path(
            sim_node_binary or default_sim_node_binary()
        )
        self.parallel = parallel

    # ------------------------------------------------------------------
    # public API
    # ------------------------------------------------------------------
    def run(self, scenario: Scenario) -> ScenarioResult:
        if not self.sim_node_binary.exists():
            raise FileNotFoundError(
                f"sim_node binary not found at {self.sim_node_binary}. "
                "Build with `cmake -S . -B build && cmake --build build`."
            )
        expand_access_pattern(scenario.topology)

        last: Optional[ScenarioResult] = None
        for rep in range(scenario.repetitions):
            run_dir = scenario.run_dir(rep)
            run_dir.mkdir(parents=True, exist_ok=True)
            (run_dir / "topology.json").write_text(
                json.dumps(scenario.topology.to_dict(), indent=2)
            )
            results = self._run_once(scenario, run_dir)
            last = ScenarioResult(
                scenario_name=scenario.name,
                run_dir=run_dir,
                nodes=results,
                succeeded=all(r.exit_code == 0 for r in results),
            )
            _dump_summary(last)
        assert last is not None
        return last

    def run_all(self, scenarios: List[Scenario]) -> List[ScenarioResult]:
        return [self.run(s) for s in scenarios]

    # ------------------------------------------------------------------
    # internals
    # ------------------------------------------------------------------
    def _run_once(
        self, scenario: Scenario, run_dir: Path
    ) -> List[NodeRunResult]:
        traces_dir = run_dir / "traces"
        traces_dir.mkdir(parents=True, exist_ok=True)
        specs = scenario.topology.nodes
        if self.parallel and len(specs) > 1:
            with ThreadPoolExecutor(max_workers=len(specs)) as ex:
                return list(
                    ex.map(
                        lambda s: self._run_node(
                            s, traces_dir, scenario.duration_ms
                        ),
                        specs,
                    )
                )
        return [
            self._run_node(s, traces_dir, scenario.duration_ms) for s in specs
        ]

    def _run_node(
        self, spec: NodeSpec, traces_dir: Path, duration_ms: int
    ) -> NodeRunResult:
        trace_path = traces_dir / f"{spec.node_id}.jsonl"
        cmd = [str(self.sim_node_binary)] + spec.cli_args(
            trace_path, duration_ms
        )
        proc = subprocess.run(
            cmd,
            check=False,
            capture_output=True,
            text=True,
        )
        published = None
        if proc.stdout:
            m = _DONE_RE.search(proc.stdout)
            if m:
                published = int(m.group(1))
        return NodeRunResult(
            node_id=spec.node_id,
            trace_path=trace_path,
            exit_code=proc.returncode,
            stdout=proc.stdout,
            stderr=proc.stderr,
            published=published,
        )


def _dump_summary(result: ScenarioResult) -> None:
    summary = {
        "scenario": result.scenario_name,
        "run_dir": str(result.run_dir),
        "succeeded": result.succeeded,
        "nodes": [
            {
                "node_id": n.node_id,
                "trace": str(n.trace_path),
                "exit_code": n.exit_code,
                "published": n.published,
            }
            for n in result.nodes
        ],
    }
    (result.run_dir / "summary.json").write_text(json.dumps(summary, indent=2))
