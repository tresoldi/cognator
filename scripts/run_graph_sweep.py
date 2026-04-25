#!/usr/bin/env python3
"""Run a graph-policy sweep for Cognator against an existing LingPy baseline."""

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
    parser.add_argument("--merkmal-dir", default="tests/fixtures/merkmal-models")
    parser.add_argument("--out-dir", required=True, help="Output directory for sweep artifacts")
    parser.add_argument("--track", default="A")
    parser.add_argument("--threshold", default="0.80", help='Fixed threshold to use; use "auto" for Otsu')
    parser.add_argument(
        "--graph-modes",
        default="threshold,support",
        help='Comma-separated graph modes; typically "threshold,support"',
    )
    parser.add_argument("--top-ks", default="2,3", help="Comma-separated top-k values for support mode")
    parser.add_argument(
        "--mutual-boosts",
        default="0.1,0.2,0.3",
        help="Comma-separated mutual-top-k boost values for support mode",
    )
    parser.add_argument(
        "--triangle-boosts",
        default="0.1,0.3,0.5",
        help="Comma-separated triangle support boost values for support mode",
    )
    parser.add_argument("--parallel-families", type=int, default=2)
    parser.add_argument("--jobs", type=int, default=0)
    parser.add_argument("--gocache", default=".gocache")
    parser.add_argument("--bootstrap", type=int, default=0, help="Optional bootstrap runs for confidence output")
    parser.add_argument("--bootstrap-sigma", type=float, default=0.05)
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
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        rows = list(reader)
    both = [row for row in rows if not row["cognator_skipped"] and not row["lingpy_skipped"]]
    mean_delta = 0.0
    wins = losses = ties = 0
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


def parameter_grid(args: argparse.Namespace) -> list[dict[str, str]]:
    rows = []
    for mode in parse_list(args.graph_modes):
        if mode == "threshold":
            rows.append(
                {
                    "label": "graph-threshold",
                    "graph_mode": "threshold",
                    "graph_top_k": "",
                    "graph_mutual_boost": "",
                    "graph_triangle_boost": "",
                }
            )
            continue
        for top_k in parse_list(args.top_ks):
            for mutual in parse_list(args.mutual_boosts):
                for triangle in parse_list(args.triangle_boosts):
                    rows.append(
                        {
                            "label": f"graph-support-k{top_k}-m{mutual}-t{triangle}",
                            "graph_mode": "support",
                            "graph_top_k": top_k,
                            "graph_mutual_boost": mutual,
                            "graph_triangle_boost": triangle,
                        }
                    )
    return rows


def main() -> None:
    args = parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["GOCACHE"] = str(Path(args.gocache))

    sweep_rows = []
    for config in parameter_grid(args):
        label = config["label"]
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
            "--merkmal-dir",
            args.merkmal_dir,
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
            "--graph-mode",
            config["graph_mode"],
        ]
        if args.jobs:
            bench_cmd.extend(["--jobs", str(args.jobs)])
        if args.bootstrap > 0:
            bench_cmd.extend(["--bootstrap", str(args.bootstrap), "--bootstrap-sigma", str(args.bootstrap_sigma)])
        if args.threshold != "auto":
            bench_cmd.extend(["--threshold", args.threshold])
        if config["graph_mode"] == "support":
            bench_cmd.extend(
                [
                    "--graph-top-k",
                    config["graph_top_k"],
                    "--graph-mutual-boost",
                    config["graph_mutual_boost"],
                    "--graph-triangle-boost",
                    config["graph_triangle_boost"],
                ]
            )
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

        cognator_errors = run_dir / "cognator_errors.tsv"
        cognator_error_summary = run_dir / "cognator_error_summary.tsv"
        error_cmd = [
            "python",
            "scripts/error_report.py",
            "--predictions",
            str(preds_tsv),
            "--out-tsv",
            str(cognator_errors),
            "--out-dataset-tsv",
            str(cognator_error_summary),
            "--label",
            label,
        ]
        run(error_cmd)

        mean_f, mean_p, mean_r, weighted_f = read_results_summary(results_tsv)
        mean_delta, wins, losses, ties = read_comparison_summary(comparison_tsv)
        sweep_rows.append(
            {
                "label": label,
                "threshold": args.threshold,
                "graph_mode": config["graph_mode"],
                "graph_top_k": config["graph_top_k"],
                "graph_mutual_boost": config["graph_mutual_boost"],
                "graph_triangle_boost": config["graph_triangle_boost"],
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
                "threshold",
                "graph_mode",
                "graph_top_k",
                "graph_mutual_boost",
                "graph_triangle_boost",
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
        handle.write("# Graph Sweep\n\n")
        handle.write("| Label | Threshold | Mode | TopK | Mutual | Triangle | Mean F1 | Mean P | Mean R | Weighted F1 | Mean Δ vs LingPy | W/L/T |\n")
        handle.write("|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        for row in sorted(sweep_rows, key=lambda item: item["mean_f1"], reverse=True):
            handle.write(
                f"| {row['label']} | {row['threshold']} | {row['graph_mode']} | {row['graph_top_k'] or '—'} | "
                f"{row['graph_mutual_boost'] or '—'} | {row['graph_triangle_boost'] or '—'} | {row['mean_f1']:.4f} | "
                f"{row['mean_precision']:.4f} | {row['mean_recall']:.4f} | {row['weighted_f1']:.4f} | "
                f"{row['mean_delta_vs_lingpy']:+.4f} | {row['wins']}/{row['losses']}/{row['ties']} |\n"
            )

    (out_dir / "meta.json").write_text(
        json.dumps(
            {
                "eval_table": args.eval_table,
                "split_file": args.split_file,
                "lingpy_results": args.lingpy_results,
                "merkmal_dir": args.merkmal_dir,
                "track": args.track,
                "threshold": args.threshold,
                "graph_modes": parse_list(args.graph_modes),
                "top_ks": parse_list(args.top_ks),
                "mutual_boosts": parse_list(args.mutual_boosts),
                "triangle_boosts": parse_list(args.triangle_boosts),
                "bootstrap": args.bootstrap,
                "bootstrap_sigma": args.bootstrap_sigma,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
