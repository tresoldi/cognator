#!/usr/bin/env python3
"""Run the full Track-A benchmark pipeline for Cognator vs LingPy.

This orchestrates:
- arcaverborum export -> Track A / Track B tables
- Cognator benchmark on Track A
- LingPy baseline on Track A
- joined comparison via cmd/benchcompare

The script is intentionally thin and shells out to the repo's native
tools instead of re-implementing benchmark logic.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--forms",
        required=True,
        help="Path to arcaverborum forms.csv or forms.tsv",
    )
    parser.add_argument(
        "--datasets-csv",
        default="../arcaverborum/datasets.csv",
        help="Path to arcaverborum datasets.csv",
    )
    parser.add_argument(
        "--bundle",
        default="tests/fixtures/merkmal/descriptive",
        help="Merkmal bundle for Cognator runs",
    )
    parser.add_argument(
        "--split-file",
        default="benchmark/splits/dev_core.txt",
        help="Dataset split file",
    )
    parser.add_argument(
        "--track",
        default="A",
        choices=["A", "B"],
        help="Benchmark track to run",
    )
    parser.add_argument(
        "--run-label",
        default="",
        help="Optional label; defaults to <track>-<split-stem>",
    )
    parser.add_argument(
        "--runs-root",
        default="benchmark/runs",
        help="Root directory for benchmark run artifacts",
    )
    parser.add_argument(
        "--export-dir",
        default="",
        help="Optional explicit export dir; defaults to the run dir's export/ subdirectory",
    )
    parser.add_argument(
        "--lingpy-method",
        default="lexstat",
        choices=["lexstat", "sca"],
        help="LingPy method",
    )
    parser.add_argument(
        "--lingpy-cluster-method",
        default="infomap",
        choices=["infomap", "upgma", "mcl", "single", "complete"],
        help="LingPy cluster method",
    )
    parser.add_argument(
        "--lingpy-runs",
        type=int,
        default=1000,
        help="LingPy LexStat scorer permutation runs",
    )
    parser.add_argument(
        "--lingpy-threshold",
        type=float,
        default=0.55,
        help="LingPy clustering threshold",
    )
    parser.add_argument(
        "--cognator-jobs",
        type=int,
        default=0,
        help="Cognator per-family worker count",
    )
    parser.add_argument(
        "--parallel-families",
        type=int,
        default=2,
        help="Cognator concurrent family count",
    )
    parser.add_argument(
        "--cognator-extra-args",
        default="",
        help="Extra raw args appended to `go run ./cmd/bench`",
    )
    parser.add_argument(
        "--lingpy-extra-args",
        default="",
        help="Extra raw args appended to the LingPy runner",
    )
    parser.add_argument(
        "--skip-export",
        action="store_true",
        help="Reuse existing export dir instead of re-running the exporter",
    )
    parser.add_argument(
        "--gocache",
        default=".gocache",
        help="Workspace-local GOCACHE path for Go commands",
    )
    return parser.parse_args()


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(shlex.quote(part) for part in cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def main() -> None:
    args = parse_args()
    split_path = Path(args.split_file)
    split_stem = split_path.stem or "split"
    run_label = args.run_label or f"{args.track.lower()}-{split_stem}"

    runs_root = Path(args.runs_root)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = runs_root / f"{timestamp}-{run_label}"
    export_dir = Path(args.export_dir) if args.export_dir else run_dir / "export"
    run_dir.mkdir(parents=True, exist_ok=True)

    track_map = {"A": "track_a.tsv", "B": "track_b.tsv"}
    eval_table = export_dir / track_map[args.track]

    if not args.skip_export:
        export_cmd = [
            "python",
            "scripts/export_arcaverborum_eval.py",
            "--forms",
            args.forms,
            "--datasets-csv",
            args.datasets_csv,
            "--out-dir",
            str(export_dir),
        ]
        run(export_cmd)
    elif not eval_table.exists():
        raise SystemExit(f"--skip-export was set, but {eval_table} does not exist")

    env = os.environ.copy()
    env["GOCACHE"] = str(Path(args.gocache))

    cognator_results = run_dir / "cognator_results.tsv"
    cognator_md = run_dir / "cognator_results.md"
    cognator_preds = run_dir / "cognator_predictions.tsv"
    bench_cmd = [
        "go",
        "run",
        "./cmd/bench",
        "--eval-table",
        str(eval_table),
        "--split-file",
        args.split_file,
        "--track",
        args.track,
        "--bundle",
        args.bundle,
        "--out-tsv",
        str(cognator_results),
        "--out-md",
        str(cognator_md),
        "--pred-out",
        str(cognator_preds),
        "--run-label",
        run_label,
        "--parallel-families",
        str(args.parallel_families),
    ]
    if args.cognator_jobs:
        bench_cmd.extend(["--jobs", str(args.cognator_jobs)])
    if args.cognator_extra_args:
        bench_cmd.extend(shlex.split(args.cognator_extra_args))
    run(bench_cmd, env=env)

    lingpy_results = run_dir / "lingpy_results.tsv"
    lingpy_preds = run_dir / "lingpy_predictions.tsv"
    lingpy_cmd = [
        "python",
        "scripts/run_lingpy_baseline.py",
        "--eval-table",
        str(eval_table),
        "--split-file",
        args.split_file,
        "--out-tsv",
        str(lingpy_results),
        "--pred-out",
        str(lingpy_preds),
        "--run-label",
        run_label,
        "--method",
        args.lingpy_method,
        "--cluster-method",
        args.lingpy_cluster_method,
        "--threshold",
        str(args.lingpy_threshold),
        "--runs",
        str(args.lingpy_runs),
    ]
    if args.lingpy_extra_args:
        lingpy_cmd.extend(shlex.split(args.lingpy_extra_args))
    run(lingpy_cmd)

    comparison_tsv = run_dir / "comparison.tsv"
    comparison_md = run_dir / "comparison.md"
    compare_cmd = [
        "go",
        "run",
        "./cmd/benchcompare",
        "--cognator",
        str(cognator_results),
        "--lingpy",
        str(lingpy_results),
        "--out-tsv",
        str(comparison_tsv),
        "--out-md",
        str(comparison_md),
    ]
    run(compare_cmd, env=env)

    cognator_errors = run_dir / "cognator_errors.tsv"
    cognator_error_summary = run_dir / "cognator_error_summary.tsv"
    cognator_error_md = run_dir / "cognator_error_summary.md"
    cognator_error_cmd = [
        "python",
        "scripts/error_report.py",
        "--predictions",
        str(cognator_preds),
        "--out-tsv",
        str(cognator_errors),
        "--out-dataset-tsv",
        str(cognator_error_summary),
        "--out-md",
        str(cognator_error_md),
        "--label",
        "cognator",
    ]
    run(cognator_error_cmd)

    lingpy_errors = run_dir / "lingpy_errors.tsv"
    lingpy_error_summary = run_dir / "lingpy_error_summary.tsv"
    lingpy_error_md = run_dir / "lingpy_error_summary.md"
    lingpy_error_cmd = [
        "python",
        "scripts/error_report.py",
        "--predictions",
        str(lingpy_preds),
        "--out-tsv",
        str(lingpy_errors),
        "--out-dataset-tsv",
        str(lingpy_error_summary),
        "--out-md",
        str(lingpy_error_md),
        "--label",
        "lingpy",
    ]
    run(lingpy_error_cmd)

    meta = {
        "run_label": run_label,
        "timestamp_utc": timestamp,
        "forms": args.forms,
        "datasets_csv": args.datasets_csv,
        "bundle": args.bundle,
        "split_file": args.split_file,
        "track": args.track,
        "export_dir": str(export_dir),
        "eval_table": str(eval_table),
        "artifacts": {
            "cognator_results_tsv": str(cognator_results),
            "cognator_results_md": str(cognator_md),
            "cognator_predictions_tsv": str(cognator_preds),
            "lingpy_results_tsv": str(lingpy_results),
            "lingpy_predictions_tsv": str(lingpy_preds),
            "comparison_tsv": str(comparison_tsv),
            "comparison_md": str(comparison_md),
            "cognator_errors_tsv": str(cognator_errors),
            "cognator_error_summary_tsv": str(cognator_error_summary),
            "cognator_error_summary_md": str(cognator_error_md),
            "lingpy_errors_tsv": str(lingpy_errors),
            "lingpy_error_summary_tsv": str(lingpy_error_summary),
            "lingpy_error_summary_md": str(lingpy_error_md),
        },
        "lingpy": {
            "method": args.lingpy_method,
            "cluster_method": args.lingpy_cluster_method,
            "threshold": args.lingpy_threshold,
            "runs": args.lingpy_runs,
        },
        "cognator": {
            "parallel_families": args.parallel_families,
            "jobs": args.cognator_jobs,
            "extra_args": args.cognator_extra_args,
        },
    }
    (run_dir / "meta.json").write_text(
        json.dumps(meta, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
