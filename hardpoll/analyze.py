"""Latency statistics for HardPoll traces.

The headline metric in the task is end-to-end latency (producer → publish).
We also surface detection latency (how quickly the poller saw the event) and
injector latency so it's easy to distinguish framework overhead from the
deliberate hardware-delay model.
"""

from __future__ import annotations

import statistics
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List

from .collector import Trace, TraceCollector


@dataclass
class LatencyStats:
    count: int = 0
    mean_ns: float = 0.0
    median_ns: float = 0.0
    p95_ns: float = 0.0
    p99_ns: float = 0.0
    p999_ns: float = 0.0
    min_ns: float = 0.0
    max_ns: float = 0.0
    stddev_ns: float = 0.0
    jitter_ns: float = 0.0  # p99 - p50, a useful tail-jitter summary

    def as_dict(self) -> Dict[str, float]:
        return {
            "count": self.count,
            "mean_ns": self.mean_ns,
            "median_ns": self.median_ns,
            "p95_ns": self.p95_ns,
            "p99_ns": self.p99_ns,
            "p999_ns": self.p999_ns,
            "min_ns": self.min_ns,
            "max_ns": self.max_ns,
            "stddev_ns": self.stddev_ns,
            "jitter_ns": self.jitter_ns,
        }


@dataclass
class NodeReport:
    node_id: str
    poll_mode: str
    end_to_end: LatencyStats
    detect: LatencyStats
    inject: LatencyStats

    def as_dict(self) -> Dict:
        return {
            "node_id": self.node_id,
            "poll_mode": self.poll_mode,
            "end_to_end": self.end_to_end.as_dict(),
            "detect": self.detect.as_dict(),
            "inject": self.inject.as_dict(),
        }


def _quantile(values: List[int], q: float) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    if q <= 0.0:
        return float(s[0])
    if q >= 1.0:
        return float(s[-1])
    # nearest-rank quantile; fine for thousands of samples
    idx = int(round(q * (len(s) - 1)))
    return float(s[idx])


def summarize(values: Iterable[int]) -> LatencyStats:
    vs = [int(v) for v in values]
    if not vs:
        return LatencyStats()
    return LatencyStats(
        count=len(vs),
        mean_ns=float(statistics.fmean(vs)),
        median_ns=_quantile(vs, 0.5),
        p95_ns=_quantile(vs, 0.95),
        p99_ns=_quantile(vs, 0.99),
        p999_ns=_quantile(vs, 0.999),
        min_ns=float(min(vs)),
        max_ns=float(max(vs)),
        stddev_ns=float(statistics.pstdev(vs)) if len(vs) > 1 else 0.0,
        jitter_ns=_quantile(vs, 0.99) - _quantile(vs, 0.5),
    )


def analyze_trace(trace: Trace) -> NodeReport:
    return NodeReport(
        node_id=trace.node_id,
        poll_mode=trace.poll_mode,
        end_to_end=summarize(trace.latencies_ns()),
        detect=summarize(trace.detect_latencies_ns()),
        inject=summarize(trace.inject_latencies_ns()),
    )


def analyze_traces(run_dir: Path) -> List[NodeReport]:
    collector = TraceCollector(run_dir)
    return [analyze_trace(t) for t in collector.load_all()]


def format_report(reports: List[NodeReport]) -> str:
    headers = [
        "node", "mode", "count", "p50(us)", "p99(us)", "p999(us)",
        "max(us)", "jitter(us)",
    ]
    widths = [max(8, len(h)) for h in headers]
    rows: List[List[str]] = []
    for r in reports:
        e = r.end_to_end
        rows.append(
            [
                r.node_id,
                r.poll_mode,
                str(e.count),
                f"{e.median_ns/1000:.2f}",
                f"{e.p99_ns/1000:.2f}",
                f"{e.p999_ns/1000:.2f}",
                f"{e.max_ns/1000:.2f}",
                f"{e.jitter_ns/1000:.2f}",
            ]
        )
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))
    out = []
    fmt = "  ".join(f"{{:<{w}}}" for w in widths)
    out.append(fmt.format(*headers))
    out.append("  ".join("-" * w for w in widths))
    for row in rows:
        out.append(fmt.format(*row))
    return "\n".join(out)
