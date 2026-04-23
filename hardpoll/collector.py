"""Trace collector: reads the JSONL traces written by each sim_node."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, List, Optional


@dataclass
class Trace:
    """In-memory representation of a single node's trace."""

    node_id: str
    records: List[dict]

    @property
    def poll_mode(self) -> str:
        return self.records[0]["poll_mode"] if self.records else "unknown"

    def latencies_ns(self) -> List[int]:
        return [r["t_publish_ns"] - r["t_produce_ns"] for r in self.records]

    def detect_latencies_ns(self) -> List[int]:
        return [r["t_detect_ns"] - r["t_produce_ns"] for r in self.records]

    def inject_latencies_ns(self) -> List[int]:
        return [r["injected_latency_ns"] for r in self.records]


class TraceCollector:
    """Reads the traces produced by a scenario run."""

    def __init__(self, run_dir: Path) -> None:
        self.run_dir = Path(run_dir)

    def iter_trace_files(self) -> Iterator[Path]:
        traces_dir = self.run_dir / "traces"
        if not traces_dir.exists():
            return iter(())
        return iter(sorted(traces_dir.glob("*.jsonl")))

    def load(self, trace_path: Path) -> Trace:
        records: List[dict] = []
        with open(trace_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                records.append(json.loads(line))
        node_id = records[0]["node_id"] if records else trace_path.stem
        return Trace(node_id=node_id, records=records)

    def load_all(self) -> List[Trace]:
        return [self.load(p) for p in self.iter_trace_files()]
