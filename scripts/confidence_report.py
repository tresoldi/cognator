#!/usr/bin/env python3
"""Summarize confidence diagnostics against pairwise error involvement.

The input is the prediction TSV emitted by `cmd/bench --pred-out`. The report
marks a form as error-involved when at least one same-concept pair containing
that form is a false merge or false split. It then summarizes confidence and
support diagnostics for error-involved versus clean forms and can emit simple
threshold curves for flagging low-confidence forms.
"""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from itertools import combinations
from pathlib import Path


METRICS = [
    "confidence",
    "assigned_support",
    "alternative_support",
    "support_margin",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, help="Prediction TSV from cmd/bench --pred-out")
    parser.add_argument("--out-tsv", required=True, help="Output TSV summary")
    parser.add_argument("--out-md", default="", help="Optional markdown summary")
    parser.add_argument("--out-curves-tsv", default="", help="Optional threshold-curve TSV")
    return parser.parse_args()


def read_predictions(path: str) -> list[dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        required = {"eval_id", "dataset", "concept", "gold", "pred", *METRICS}
        missing = required - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"missing required columns: {sorted(missing)}")
        return list(reader)


def error_involvement(rows: list[dict[str, str]]) -> dict[str, set[str]]:
    out = {
        "error_involved": set(),
        "false_merge_involved": set(),
        "false_split_involved": set(),
    }
    by_concept: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row["gold"]:
            by_concept[(row["dataset"], row["concept"])].append(row)
    for members in by_concept.values():
        for left, right in combinations(members, 2):
            gold_same = left["gold"] == right["gold"]
            pred_same = left["pred"] == right["pred"]
            if not gold_same and pred_same:
                out["error_involved"].update([left["eval_id"], right["eval_id"]])
                out["false_merge_involved"].update([left["eval_id"], right["eval_id"]])
            elif gold_same and not pred_same:
                out["error_involved"].update([left["eval_id"], right["eval_id"]])
                out["false_split_involved"].update([left["eval_id"], right["eval_id"]])
    return out


def quantile(sorted_values: list[float], q: float) -> float:
    if not sorted_values:
        return 0.0
    if q <= 0:
        return sorted_values[0]
    if q >= 1:
        return sorted_values[-1]
    idx = int((len(sorted_values) - 1) * q)
    return sorted_values[idx]


def summarize_values(values: list[float]) -> dict[str, float | int]:
    xs = sorted(values)
    if not xs:
        return {"n": 0, "mean": 0.0, "p10": 0.0, "p50": 0.0, "p90": 0.0}
    return {
        "n": len(xs),
        "mean": statistics.mean(xs),
        "p10": quantile(xs, 0.10),
        "p50": quantile(xs, 0.50),
        "p90": quantile(xs, 0.90),
    }


def build_summary(rows: list[dict[str, str]], involvement: dict[str, set[str]]) -> list[dict[str, object]]:
    groups: dict[str, list[dict[str, str]]] = {
        "error_involved": [row for row in rows if row["eval_id"] in involvement["error_involved"]],
        "false_merge_involved": [row for row in rows if row["eval_id"] in involvement["false_merge_involved"]],
        "false_split_involved": [row for row in rows if row["eval_id"] in involvement["false_split_involved"]],
        "clean": [row for row in rows if row["eval_id"] not in involvement["error_involved"]],
    }
    out: list[dict[str, object]] = []
    for group, members in groups.items():
        for metric in METRICS:
            values = [float(row[metric]) for row in members]
            summary = summarize_values(values)
            out.append({"group": group, "metric": metric, **summary})
    return out


def threshold_grid(rows: list[dict[str, str]], metric: str) -> list[float]:
    values = sorted({float(row[metric]) for row in rows})
    if not values:
        return []
    candidates = [
        quantile(values, 0.01),
        quantile(values, 0.05),
        quantile(values, 0.10),
        quantile(values, 0.20),
        quantile(values, 0.30),
        quantile(values, 0.50),
    ]
    return sorted(set(candidates))


def metric_flags(value: float, metric: str, threshold: float) -> bool:
    if metric in {"confidence", "support_margin"}:
        return value <= threshold
    return value >= threshold


def build_curves(rows: list[dict[str, str]], involvement: dict[str, set[str]]) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    targets = {
        "error_involved": involvement["error_involved"],
        "false_merge_involved": involvement["false_merge_involved"],
        "false_split_involved": involvement["false_split_involved"],
    }
    curve_metrics = ["confidence", "support_margin", "alternative_support"]
    total = len(rows)
    for metric in curve_metrics:
        thresholds = threshold_grid(rows, metric)
        if metric == "alternative_support":
            values = sorted({float(row[metric]) for row in rows})
            thresholds = sorted(set(thresholds + [quantile(values, q) for q in (0.70, 0.80, 0.90, 0.95)]))
        for threshold in thresholds:
            flagged = [row for row in rows if metric_flags(float(row[metric]), metric, threshold)]
            flagged_ids = {row["eval_id"] for row in flagged}
            for target_name, target_ids in targets.items():
                hits = len(flagged_ids & target_ids)
                out.append(
                    {
                        "metric": metric,
                        "direction": "<=" if metric in {"confidence", "support_margin"} else ">=",
                        "threshold": threshold,
                        "target": target_name,
                        "flagged": len(flagged_ids),
                        "target_total": len(target_ids),
                        "hits": hits,
                        "precision": hits / len(flagged_ids) if flagged_ids else 0.0,
                        "recall": hits / len(target_ids) if target_ids else 0.0,
                        "flag_rate": len(flagged_ids) / total if total else 0.0,
                    }
                )
    return out


def write_tsv(path: str, rows: list[dict[str, object]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fieldnames = ["group", "metric", "n", "mean", "p10", "p50", "p90"]
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def write_curves_tsv(path: str, rows: list[dict[str, object]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "metric",
        "direction",
        "threshold",
        "target",
        "flagged",
        "target_total",
        "hits",
        "precision",
        "recall",
        "flag_rate",
    ]
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def write_md(path: str, rows: list[dict[str, object]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("w", encoding="utf-8") as handle:
        handle.write("# Confidence Diagnostic Report\n\n")
        handle.write("| Group | Metric | N | Mean | P10 | P50 | P90 |\n")
        handle.write("|---|---|---:|---:|---:|---:|---:|\n")
        for row in rows:
            handle.write(
                f"| {row['group']} | {row['metric']} | {row['n']} | "
                f"{row['mean']:.4f} | {row['p10']:.4f} | {row['p50']:.4f} | {row['p90']:.4f} |\n"
            )


def main() -> None:
    args = parse_args()
    rows = read_predictions(args.predictions)
    involvement = error_involvement(rows)
    summary = build_summary(rows, involvement)
    write_tsv(args.out_tsv, summary)
    if args.out_curves_tsv:
        write_curves_tsv(args.out_curves_tsv, build_curves(rows, involvement))
    if args.out_md:
        write_md(args.out_md, summary)


if __name__ == "__main__":
    main()
