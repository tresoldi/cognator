#!/usr/bin/env python3
"""Run a fixed-threshold sweep for Cognator against an existing LingPy baseline.

This is the first Wave 2 experiment: measure how much of the current gap is
explained by threshold calibration alone before implementing new thresholding
strategies.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shlex
import subprocess
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eval-table", required=True, help="Exported Track A/Track B TSV")
    parser.add_argument("--split-file", required=True, help="Dataset split file")
    parser.add_argument("--lingpy-results", required=True, help="Existing LingPy results TSV")
    parser.add_argument("--bundle", default="tests/fixtures/merkmal/descriptive")
    parser.add_argument("--out-dir", required=True, help="Output directory for sweep artifacts")
    parser.add_argument(
        "--thresholds",
        default="auto,0.45,0.50,0.55,0.60,0.65,0.70,0.75,0.80",
        help='Comma-separated thresholds; use "auto" for Otsu',
    )
    parser.add_argument("--track", default="A")
    parser.add_argument(
        "--threshold-mode",
        default="family",
        choices=["family", "concept-shrink"],
        help="Cognator threshold mode to evaluate",
    )
    parser.add_argument(
        "--threshold-shrinks",
        default="0.5",
        help="Comma-separated shrinkage values for concept-shrink mode",
    )
    parser.add_argument("--parallel-families", type=int, default=2)
    parser.add_argument("--jobs", type=int, default=0)
    parser.add_argument("--gocache", default=".gocache")
    return parser.parse_args()


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(shlex.quote(part) for part in cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def parse_thresholds(spec: str) -> list[str]:
    out = []
    for item in spec.split(","):
        item = item.strip()
        if item:
            out.append(item)
    return out


def parse_shrinks(spec: str) -> list[str]:
    out = []
    for item in spec.split(","):
        item = item.strip()
        if item:
            out.append(item)
    return out


def read_results_summary(path: Path) -> tuple[float, float, float, float]:
    scored = 0
    sum_f = 0.0
    sum_p = 0.0
    sum_r = 0.0
    weighted_f = 0.0
    total_forms = 0
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            if row["skipped"]:
                continue
            scored += 1
            f1 = float(row["f1"])
            p = float(row["precision"])
            r = float(row["recall"])
            cognacy_forms = int(row["cognacy_forms"])
            sum_f += f1
            sum_p += p
            sum_r += r
            weighted_f += f1 * cognacy_forms
            total_forms += cognacy_forms
    mean_f = sum_f / scored if scored else 0.0
    mean_p = sum_p / scored if scored else 0.0
    mean_r = sum_r / scored if scored else 0.0
    w_f = weighted_f / total_forms if total_forms else 0.0
    return mean_f, mean_p, mean_r, w_f


def read_comparison_summary(path: Path) -> tuple[float, int, int, int]:
    mean_delta = 0.0
    wins = losses = ties = 0
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        rows = list(reader)
    both = [row for row in rows if not row["cognator_skipped"] and not row["lingpy_skipped"]]
    if both:
        deltas = [float(row["delta_F"]) for row in both]
        mean_delta = sum(deltas) / len(deltas)
        for delta in deltas:
            if delta > 1e-6:
                wins += 1
            elif delta < -1e-6:
                losses += 1
            else:
                ties += 1
    return mean_delta, wins, losses, ties


def main() -> None:
    args = parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["GOCACHE"] = str(Path(args.gocache))

    sweep_rows = []
    thresholds = parse_thresholds(args.thresholds)
    shrinks = parse_shrinks(args.threshold_shrinks)
    if args.threshold_mode == "family":
        shrinks = [""]

    for shrink in shrinks:
        for threshold in thresholds:
            label_parts = []
            if args.threshold_mode != "family":
                label_parts.append(args.threshold_mode)
                label_parts.append(f"s{shrink}")
            label_parts.append("auto" if threshold == "auto" else f"t{threshold}")
            label = "-".join(label_parts)
            run_dir = out_dir / label
            run_dir.mkdir(parents=True, exist_ok=True)

            results_tsv = run_dir / "cognator_results.tsv"
            results_md = run_dir / "cognator_results.md"
            preds_tsv = run_dir / "cognator_predictions.tsv"
            bench_cmd = [
                "go",
                "run",
                "./cmd/bench",
                "--eval-table",
                args.eval_table,
                "--split-file",
                args.split_file,
                "--track",
                args.track,
                "--bundle",
                args.bundle,
                "--out-tsv",
                str(results_tsv),
                "--out-md",
                str(results_md),
                "--pred-out",
                str(preds_tsv),
                "--run-label",
                label,
                "--parallel-families",
                str(args.parallel_families),
                "--threshold-mode",
                args.threshold_mode,
            ]
            if args.threshold_mode == "concept-shrink" and shrink != "":
                bench_cmd.extend(["--threshold-shrink", shrink])
            if args.jobs:
                bench_cmd.extend(["--jobs", str(args.jobs)])
            if threshold != "auto":
                bench_cmd.extend(["--threshold", threshold])
            run(bench_cmd, env=env)

            comparison_tsv = run_dir / "comparison.tsv"
            comparison_md = run_dir / "comparison.md"
            compare_cmd = [
                "go",
                "run",
                "./cmd/benchcompare",
                "--cognator",
                str(results_tsv),
                "--lingpy",
                args.lingpy_results,
                "--out-tsv",
                str(comparison_tsv),
                "--out-md",
                str(comparison_md),
            ]
            run(compare_cmd, env=env)

            mean_f, mean_p, mean_r, weighted_f = read_results_summary(results_tsv)
            mean_delta, wins, losses, ties = read_comparison_summary(comparison_tsv)
            sweep_rows.append(
                {
                    "label": label,
                    "threshold_mode": args.threshold_mode,
                    "threshold_shrink": shrink if shrink != "" else "",
                    "threshold": threshold,
                    "mean_f1": mean_f,
                    "mean_precision": mean_p,
                    "mean_recall": mean_r,
                    "weighted_f1": weighted_f,
                    "mean_delta_vs_lingpy": mean_delta,
                    "wins": wins,
                    "losses": losses,
                    "ties": ties,
                }
            )

    summary_tsv = out_dir / "summary.tsv"
    with summary_tsv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "label",
                "threshold_mode",
                "threshold_shrink",
                "threshold",
                "mean_f1",
                "mean_precision",
                "mean_recall",
                "weighted_f1",
                "mean_delta_vs_lingpy",
                "wins",
                "losses",
                "ties",
            ],
            delimiter="\t",
        )
        writer.writeheader()
        for row in sweep_rows:
            writer.writerow(row)

    summary_md = out_dir / "summary.md"
    with summary_md.open("w", encoding="utf-8") as handle:
        handle.write("# Threshold Sweep\n\n")
        handle.write("| Label | Mode | Shrink | Threshold | Mean F1 | Mean P | Mean R | Weighted F1 | Mean Δ vs LingPy | W/L/T |\n")
        handle.write("|---|---|---|---|---|---|---|---|---|---|\n")
        for row in sorted(sweep_rows, key=lambda item: item["mean_f1"], reverse=True):
            handle.write(
                f"| {row['label']} | {row['threshold_mode']} | {row['threshold_shrink'] or '—'} | {row['threshold']} | {row['mean_f1']:.4f} | "
                f"{row['mean_precision']:.4f} | {row['mean_recall']:.4f} | {row['weighted_f1']:.4f} | "
                f"{row['mean_delta_vs_lingpy']:+.4f} | {row['wins']}/{row['losses']}/{row['ties']} |\n"
            )

    (out_dir / "meta.json").write_text(
        json.dumps(
            {
                "eval_table": args.eval_table,
                "split_file": args.split_file,
                "lingpy_results": args.lingpy_results,
                "bundle": args.bundle,
                "track": args.track,
                "threshold_mode": args.threshold_mode,
                "thresholds": parse_thresholds(args.thresholds),
                "threshold_shrinks": parse_shrinks(args.threshold_shrinks),
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
