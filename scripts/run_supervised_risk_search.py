#!/usr/bin/env python3
"""Run a compact supervised search over continuous edge-risk weights.

This optimizes the calibrated risk model directly against a labeled dev split
by sampling penalty/floor values and feature-weight blends, evaluating each
candidate through the normal benchmark pipeline, and optionally validating the
best candidate on a secondary split.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import random
import shlex
import subprocess
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eval-table", required=True)
    parser.add_argument("--dev-split-file", required=True)
    parser.add_argument("--lingpy-results", required=True)
    parser.add_argument("--secondary-split-file", default="")
    parser.add_argument("--secondary-lingpy-results", default="")
    parser.add_argument("--merkmal-dir", default="tests/fixtures/merkmal-models")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--track", default="A")
    parser.add_argument("--gocache", default=".gocache")
    parser.add_argument("--parallel-families", type=int, default=2)
    parser.add_argument("--jobs", type=int, default=0)
    parser.add_argument("--trials", type=int, default=8)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--threshold", default="0.68")
    parser.add_argument("--graph-mode", default="support")
    parser.add_argument("--graph-expansion-mode", default="adaptive")
    parser.add_argument("--graph-top-k", default="3")
    parser.add_argument("--graph-adaptive-margin", default="0.1")
    parser.add_argument("--graph-adaptive-either-max", default="0.15")
    parser.add_argument("--graph-adaptive-ambiguity", default="0.5")
    parser.add_argument("--graph-mutual-boost", default="0.2")
    parser.add_argument("--graph-triangle-boost", default="0.3")
    parser.add_argument("--refine-mode", default="bridge-cut")
    parser.add_argument("--refine-confidence-threshold", default="0.55")
    parser.add_argument("--refine-bridge-ratio", default="0.7")
    parser.add_argument("--penalty-min", type=float, default=0.05)
    parser.add_argument("--penalty-max", type=float, default=0.35)
    parser.add_argument("--floor-min", type=float, default=0.02)
    parser.add_argument("--floor-max", type=float, default=0.10)
    return parser.parse_args()


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(shlex.quote(part) for part in cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def read_results_summary(path: Path) -> tuple[float, float, float]:
    scored = 0
    sum_f = 0.0
    sum_p = 0.0
    sum_r = 0.0
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            if row["skipped"]:
                continue
            scored += 1
            sum_f += float(row["f1"])
            sum_p += float(row["precision"])
            sum_r += float(row["recall"])
    if scored == 0:
        return 0.0, 0.0, 0.0
    return sum_f / scored, sum_p / scored, sum_r / scored


def read_comparison_summary(path: Path) -> tuple[float, int, int, int]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        rows = list(reader)
    both = [row for row in rows if not row["cognator_skipped"] and not row["lingpy_skipped"]]
    if not both:
        return 0.0, 0, 0, 0
    deltas = [float(row["delta_F"]) for row in both]
    mean_delta = sum(deltas) / len(deltas)
    wins = losses = ties = 0
    for delta in deltas:
        if delta > 1e-6:
            wins += 1
        elif delta < -1e-6:
            losses += 1
        else:
            ties += 1
    return mean_delta, wins, losses, ties


def run_config(
    *,
    eval_table: str,
    split_file: str,
    lingpy_results: str,
    merkmal_dir: str,
    out_dir: Path,
    env: dict[str, str],
    track: str,
    parallel_families: int,
    jobs: int,
    label: str,
    flags: list[str],
) -> dict[str, object]:
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
        eval_table,
        "--split-file",
        split_file,
        "--track",
        track,
        "--merkmal-dir",
        merkmal_dir,
        "--out-tsv",
        str(results_tsv),
        "--out-md",
        str(results_md),
        "--pred-out",
        str(preds_tsv),
        "--run-label",
        label,
        "--parallel-families",
        str(parallel_families),
    ] + flags
    if jobs:
        bench_cmd.extend(["--jobs", str(jobs)])
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
        lingpy_results,
        "--out-tsv",
        str(comparison_tsv),
        "--out-md",
        str(comparison_md),
    ]
    run(compare_cmd, env=env)

    mean_f, mean_p, mean_r = read_results_summary(results_tsv)
    mean_delta, wins, losses, ties = read_comparison_summary(comparison_tsv)
    return {
        "label": label,
        "results_tsv": str(results_tsv),
        "comparison_tsv": str(comparison_tsv),
        "mean_f1": mean_f,
        "mean_precision": mean_p,
        "mean_recall": mean_r,
        "mean_delta_vs_lingpy": mean_delta,
        "wins": wins,
        "losses": losses,
        "ties": ties,
    }


def main() -> None:
    args = parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["GOCACHE"] = str(Path(args.gocache))
    rng = random.Random(args.seed)

    common_flags = [
        "--threshold",
        args.threshold,
        "--graph-mode",
        args.graph_mode,
        "--graph-expansion-mode",
        args.graph_expansion_mode,
        "--graph-top-k",
        args.graph_top_k,
        "--graph-adaptive-margin",
        args.graph_adaptive_margin,
        "--graph-adaptive-either-max",
        args.graph_adaptive_either_max,
        "--graph-adaptive-ambiguity",
        args.graph_adaptive_ambiguity,
        "--graph-mutual-boost",
        args.graph_mutual_boost,
        "--graph-triangle-boost",
        args.graph_triangle_boost,
        "--refine-mode",
        args.refine_mode,
        "--refine-confidence-threshold",
        args.refine_confidence_threshold,
        "--refine-bridge-ratio",
        args.refine_bridge_ratio,
    ]

    rows: list[dict[str, object]] = []
    for trial in range(args.trials):
        penalty = rng.uniform(args.penalty_min, args.penalty_max)
        floor = rng.uniform(args.floor_min, args.floor_max)
        weights = [rng.random() for _ in range(6)]
        total = sum(weights)
        weights = [w / total for w in weights]
        label = f"trial-{trial:02d}"
        flags = common_flags + [
            "--graph-contrastive-penalty",
            f"{penalty:.4f}",
            "--graph-contrastive-floor",
            f"{floor:.4f}",
            "--graph-risk-rank-weight",
            f"{weights[0]:.4f}",
            "--graph-risk-gap-weight",
            f"{weights[1]:.4f}",
            "--graph-risk-triangle-weight",
            f"{weights[2]:.4f}",
            "--graph-risk-unilateral-weight",
            f"{weights[3]:.4f}",
            "--graph-risk-density-weight",
            f"{weights[4]:.4f}",
            "--graph-risk-dispersion-weight",
            f"{weights[5]:.4f}",
        ]
        row = run_config(
            eval_table=args.eval_table,
            split_file=args.dev_split_file,
            lingpy_results=args.lingpy_results,
            merkmal_dir=args.merkmal_dir,
            out_dir=out_dir / "dev",
            env=env,
            track=args.track,
            parallel_families=args.parallel_families,
            jobs=args.jobs,
            label=label,
            flags=flags,
        )
        row.update(
            {
                "penalty": penalty,
                "floor": floor,
                "risk_rank_weight": weights[0],
                "risk_gap_weight": weights[1],
                "risk_triangle_weight": weights[2],
                "risk_unilateral_weight": weights[3],
                "risk_density_weight": weights[4],
                "risk_dispersion_weight": weights[5],
            }
        )
        rows.append(row)

    rows.sort(key=lambda row: (row["mean_f1"], row["mean_delta_vs_lingpy"]), reverse=True)
    best = rows[0] if rows else None

    summary_tsv = out_dir / "summary.tsv"
    with summary_tsv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "label",
                "penalty",
                "floor",
                "risk_rank_weight",
                "risk_gap_weight",
                "risk_triangle_weight",
                "risk_unilateral_weight",
                "risk_density_weight",
                "risk_dispersion_weight",
                "mean_f1",
                "mean_precision",
                "mean_recall",
                "mean_delta_vs_lingpy",
                "wins",
                "losses",
                "ties",
            ],
            delimiter="\t",
        )
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row[k] for k in writer.fieldnames})

    summary_md = out_dir / "summary.md"
    with summary_md.open("w", encoding="utf-8") as handle:
        handle.write("# Supervised Risk Search\n\n")
        handle.write("| Label | Penalty | Floor | Weights (R/G/T/U/D/Disp) | Mean F1 | Mean P | Mean R | Mean Δ vs LingPy | W/L/T |\n")
        handle.write("|---|---|---|---|---|---|---|---|---|\n")
        for row in rows:
            weights = (
                f"{row['risk_rank_weight']:.2f}/"
                f"{row['risk_gap_weight']:.2f}/"
                f"{row['risk_triangle_weight']:.2f}/"
                f"{row['risk_unilateral_weight']:.2f}/"
                f"{row['risk_density_weight']:.2f}/"
                f"{row['risk_dispersion_weight']:.2f}"
            )
            handle.write(
                f"| {row['label']} | {row['penalty']:.3f} | {row['floor']:.3f} | {weights} | "
                f"{row['mean_f1']:.4f} | {row['mean_precision']:.4f} | {row['mean_recall']:.4f} | "
                f"{row['mean_delta_vs_lingpy']:+.4f} | {row['wins']}/{row['losses']}/{row['ties']} |\n"
            )
        if best:
            handle.write("\n## Best Dev Config\n\n")
            handle.write(f"- label: `{best['label']}`\n")
            handle.write(f"- mean F1: `{best['mean_f1']:.4f}`\n")
            handle.write(f"- mean delta vs LingPy: `{best['mean_delta_vs_lingpy']:+.4f}`\n")

    secondary_row = None
    if best and args.secondary_split_file and args.secondary_lingpy_results:
        flags = common_flags + [
            "--graph-contrastive-penalty",
            f"{best['penalty']:.6f}",
            "--graph-contrastive-floor",
            f"{best['floor']:.6f}",
            "--graph-risk-rank-weight",
            f"{best['risk_rank_weight']:.6f}",
            "--graph-risk-gap-weight",
            f"{best['risk_gap_weight']:.6f}",
            "--graph-risk-triangle-weight",
            f"{best['risk_triangle_weight']:.6f}",
            "--graph-risk-unilateral-weight",
            f"{best['risk_unilateral_weight']:.6f}",
            "--graph-risk-density-weight",
            f"{best['risk_density_weight']:.6f}",
            "--graph-risk-dispersion-weight",
            f"{best['risk_dispersion_weight']:.6f}",
        ]
        secondary_row = run_config(
            eval_table=args.eval_table,
            split_file=args.secondary_split_file,
            lingpy_results=args.secondary_lingpy_results,
            merkmal_dir=args.merkmal_dir,
            out_dir=out_dir / "secondary",
            env=env,
            track=args.track,
            parallel_families=args.parallel_families,
            jobs=args.jobs,
            label=str(best["label"]),
            flags=flags,
        )

    meta = {
        "eval_table": args.eval_table,
        "dev_split_file": args.dev_split_file,
        "lingpy_results": args.lingpy_results,
        "secondary_split_file": args.secondary_split_file,
        "secondary_lingpy_results": args.secondary_lingpy_results,
        "best_dev": best,
        "secondary_result": secondary_row,
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
