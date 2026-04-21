#!/usr/bin/env python3
"""Run a small dev-only calibration sweep for edge-risk weights.

This script searches over a compact grid of contrastive-penalty settings and
local risk-feature blends, scores each configuration on a development split,
and can optionally evaluate the best configuration on a secondary split.
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
    parser.add_argument("--dev-split-file", required=True, help="Development split file")
    parser.add_argument("--lingpy-results", required=True, help="LingPy results TSV for the dev split")
    parser.add_argument("--bundle", default="tests/fixtures/merkmal/descriptive")
    parser.add_argument("--out-dir", required=True, help="Output directory for sweep artifacts")
    parser.add_argument("--track", default="A")
    parser.add_argument("--threshold", default="0.68")
    parser.add_argument("--parallel-families", type=int, default=2)
    parser.add_argument("--jobs", type=int, default=0)
    parser.add_argument("--gocache", default=".gocache")
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
    parser.add_argument(
        "--penalties",
        default="0.10,0.20,0.30",
        help="Comma-separated contrastive penalty strengths",
    )
    parser.add_argument(
        "--floors",
        default="0.05",
        help="Comma-separated contrastive floors",
    )
    parser.add_argument(
        "--risk-weight-specs",
        default="rg:0.5:0.5:0.0:0.0:0.0:0.0,rgt:0.35:0.35:0.3:0.0:0.0:0.0,mix:0.3:0.3:0.2:0.2:0.0:0.0,d1:0.0:0.0:0.0:0.0:1.0:0.0,d2:0.0:0.0:0.0:0.0:0.0:1.0",
        help="Comma-separated label:rank:gap:triangle:unilateral:density:dispersion specs",
    )
    parser.add_argument("--secondary-split-file", default="", help="Optional secondary split for best-config evaluation")
    parser.add_argument("--secondary-lingpy-results", default="", help="Optional LingPy results TSV for the secondary split")
    return parser.parse_args()


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(shlex.quote(part) for part in cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def parse_list(spec: str) -> list[str]:
    out = []
    for item in spec.split(","):
        item = item.strip()
        if item:
            out.append(item)
    return out


def parse_weight_specs(spec: str) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for item in parse_list(spec):
        parts = item.split(":")
        if len(parts) == 5:
            label, rank, gap, tri, unilateral = parts
            density = "0.0"
            dispersion = "0.0"
        elif len(parts) == 7:
            label, rank, gap, tri, unilateral, density, dispersion = parts
        else:
            raise ValueError(f"invalid risk-weight spec {item!r}")
        rows.append(
            {
                "label": label,
                "rank": rank,
                "gap": gap,
                "triangle": tri,
                "unilateral": unilateral,
                "density": density,
                "dispersion": dispersion,
            }
        )
    return rows


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
    bundle: str,
    out_dir: Path,
    env: dict[str, str],
    track: str,
    parallel_families: int,
    jobs: int,
    base_flags: list[str],
    label: str,
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
        "--bundle",
        bundle,
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
    ] + base_flags
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

    sweep_rows: list[dict[str, object]] = []
    for penalty in parse_list(args.penalties):
        for floor in parse_list(args.floors):
            for spec in parse_weight_specs(args.risk_weight_specs):
                label = f"risk-{spec['label']}-p{penalty}-f{floor}"
                flags = common_flags + [
                    "--graph-contrastive-penalty",
                    penalty,
                    "--graph-contrastive-floor",
                    floor,
                    "--graph-risk-rank-weight",
                    spec["rank"],
                    "--graph-risk-gap-weight",
                    spec["gap"],
                    "--graph-risk-triangle-weight",
                    spec["triangle"],
                    "--graph-risk-unilateral-weight",
                    spec["unilateral"],
                    "--graph-risk-density-weight",
                    spec["density"],
                    "--graph-risk-dispersion-weight",
                    spec["dispersion"],
                ]
                row = run_config(
                    eval_table=args.eval_table,
                    split_file=args.dev_split_file,
                    lingpy_results=args.lingpy_results,
                    bundle=args.bundle,
                    out_dir=out_dir / "dev",
                    env=env,
                    track=args.track,
                    parallel_families=args.parallel_families,
                    jobs=args.jobs,
                    base_flags=flags,
                    label=label,
                )
                row.update(
                    {
                        "penalty": float(penalty),
                        "floor": float(floor),
                        "risk_label": spec["label"],
                        "risk_rank_weight": float(spec["rank"]),
                        "risk_gap_weight": float(spec["gap"]),
                        "risk_triangle_weight": float(spec["triangle"]),
                        "risk_unilateral_weight": float(spec["unilateral"]),
                        "risk_density_weight": float(spec["density"]),
                        "risk_dispersion_weight": float(spec["dispersion"]),
                    }
                )
                sweep_rows.append(row)

    sweep_rows.sort(key=lambda row: (row["mean_f1"], row["mean_delta_vs_lingpy"]), reverse=True)
    best = sweep_rows[0] if sweep_rows else None

    summary_tsv = out_dir / "summary.tsv"
    with summary_tsv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "label",
                "penalty",
                "floor",
                "risk_label",
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
        for row in sweep_rows:
            writer.writerow({k: row[k] for k in writer.fieldnames})

    summary_md = out_dir / "summary.md"
    with summary_md.open("w", encoding="utf-8") as handle:
        handle.write("# Risk Calibration Sweep\n\n")
        handle.write("| Label | Penalty | Floor | Weights (R/G/T/U/D/Disp) | Mean F1 | Mean P | Mean R | Mean Δ vs LingPy | W/L/T |\n")
        handle.write("|---|---|---|---|---|---|---|---|---|\n")
        for row in sweep_rows:
            weights = (
                f"{row['risk_rank_weight']:.2f}/"
                f"{row['risk_gap_weight']:.2f}/"
                f"{row['risk_triangle_weight']:.2f}/"
                f"{row['risk_unilateral_weight']:.2f}/"
                f"{row['risk_density_weight']:.2f}/"
                f"{row['risk_dispersion_weight']:.2f}"
            )
            handle.write(
                f"| {row['label']} | {row['penalty']:.2f} | {row['floor']:.2f} | {weights} | "
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
            str(best["penalty"]),
            "--graph-contrastive-floor",
            str(best["floor"]),
            "--graph-risk-rank-weight",
            str(best["risk_rank_weight"]),
            "--graph-risk-gap-weight",
            str(best["risk_gap_weight"]),
            "--graph-risk-triangle-weight",
            str(best["risk_triangle_weight"]),
            "--graph-risk-unilateral-weight",
            str(best["risk_unilateral_weight"]),
            "--graph-risk-density-weight",
            str(best["risk_density_weight"]),
            "--graph-risk-dispersion-weight",
            str(best["risk_dispersion_weight"]),
        ]
        secondary_row = run_config(
            eval_table=args.eval_table,
            split_file=args.secondary_split_file,
            lingpy_results=args.secondary_lingpy_results,
            bundle=args.bundle,
            out_dir=out_dir / "secondary",
            env=env,
            track=args.track,
            parallel_families=args.parallel_families,
            jobs=args.jobs,
            base_flags=flags,
            label=str(best["label"]),
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
