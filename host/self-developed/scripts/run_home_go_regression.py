#!/usr/bin/env python3
"""Compare deterministic Home/Go inference JSONL traces in 250 ms buckets.

Each line must contain timestamp_ms and may contain presence, prone, level.
Inference cadence is derived from consecutive timestamp_ms values. The script
does not fabricate visual labels and does not execute the NPU by itself.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if "timestamp_ms" not in row:
                raise ValueError(f"{path}:{line_number}: timestamp_ms is required")
            rows.append(row)
    rows.sort(key=lambda row: int(row["timestamp_ms"]))
    if not rows:
        raise ValueError(f"{path}: trace is empty")
    return rows


def bucketize(rows: list[dict[str, Any]], bucket_ms: int) -> dict[int, dict[str, Any]]:
    buckets: dict[int, dict[str, Any]] = {}
    for row in rows:
        bucket = int(row["timestamp_ms"]) // bucket_ms
        buckets[bucket] = row
    return buckets


def agreement(home: dict[int, dict[str, Any]], go: dict[int, dict[str, Any]], field: str) -> float:
    common = sorted(set(home) & set(go))
    comparable = [bucket for bucket in common if home[bucket].get(field) is not None and go[bucket].get(field) is not None]
    if not comparable:
        return 1.0
    return sum(home[bucket].get(field) == go[bucket].get(field) for bucket in comparable) / len(comparable)


def transition_count(rows: list[dict[str, Any]], field: str) -> int:
    values = [row.get(field) for row in rows if row.get(field) is not None]
    return sum(left != right for left, right in zip(values, values[1:]))


def percentile(values: list[int], quantile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return float(ordered[lower])
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def cadence(rows: list[dict[str, Any]]) -> dict[str, float]:
    stamps = [int(row["timestamp_ms"]) for row in rows]
    intervals = [right - left for left, right in zip(stamps, stamps[1:]) if right > left]
    duration_ms = max(1, stamps[-1] - stamps[0])
    return {
        "fps": (len(stamps) - 1) * 1000.0 / duration_ms if len(stamps) > 1 else 0.0,
        "interval_p99_ms": percentile(intervals, 0.99),
        "interval_max_ms": float(max(intervals, default=0)),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--home", type=Path, required=True)
    parser.add_argument("--go", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    manifest = load_json(args.manifest)
    thresholds = manifest["thresholds"]
    bucket_ms = int(manifest.get("bucket_ms", 250))
    home_rows = load_jsonl(args.home)
    go_rows = load_jsonl(args.go)
    home_buckets = bucketize(home_rows, bucket_ms)
    go_buckets = bucketize(go_rows, bucket_ms)

    metrics: dict[str, Any] = {
        "presence_agreement": agreement(home_buckets, go_buckets, "presence"),
        "prone_agreement": agreement(home_buckets, go_buckets, "prone"),
        "level_agreement": agreement(home_buckets, go_buckets, "level"),
        "transition_count_delta": {
            field: abs(transition_count(home_rows, field) - transition_count(go_rows, field))
            for field in ("presence", "prone", "level")
        },
        "home_cadence": cadence(home_rows),
        "go_cadence": cadence(go_rows),
    }
    failures: list[str] = []
    for field in ("presence", "prone", "level"):
        key = f"{field}_agreement"
        if metrics[key] < float(thresholds[f"{key}_min"]):
            failures.append(key)
        if metrics["transition_count_delta"][field] > int(thresholds["transition_count_delta_max"]):
            failures.append(f"{field}_transition_count_delta")
    go_cadence = metrics["go_cadence"]
    if go_cadence["fps"] < float(thresholds["go_inference_fps_min"]):
        failures.append("go_inference_fps")
    if go_cadence["interval_p99_ms"] >= float(thresholds["inference_interval_p99_ms_max"]):
        failures.append("go_interval_p99")
    if go_cadence["interval_max_ms"] >= float(thresholds["inference_interval_max_ms_max"]):
        failures.append("go_interval_max")

    report = {"passed": not failures, "failures": failures, "metrics": metrics}
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
