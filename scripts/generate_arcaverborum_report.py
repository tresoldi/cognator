#!/usr/bin/env python3
"""Generate a Cognator-vs-LingPy performance report for arcaverborum data.

The script has two workflows:

1. Existing mode, the default: read existing benchmark run directories and
   regenerate a detailed markdown/TSV report.
2. Fresh mode: export arcaverborum data, run the C/Python benchmark harness and
   LingPy for selected splits, generate comparisons and diagnostics, then write
   the same report.

The report intentionally works from benchmark artifacts rather than hard-coded
numbers, so it can be rerun after future experiments.

Fresh mode uses C/Python Cognator scripts and no longer depends on Go.
"""

from __future__ import annotations

import argparse
import csv
import json
import shlex
import subprocess
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean, median


CURRENT_COGNATOR_ARGS = (
    "--threshold 0.68 "
    "--graph-mode support "
    "--graph-expansion-mode adaptive "
    "--graph-top-k 3 "
    "--graph-adaptive-margin 0.1 "
    "--graph-adaptive-either-max 0.15 "
    "--graph-adaptive-ambiguity 0.5 "
    "--graph-mutual-boost 0.2 "
    "--graph-triangle-boost 0.3 "
    "--graph-contrastive-penalty 0.20 "
    "--graph-contrastive-floor 0.05 "
    "--graph-risk-rank-weight 0.5 "
    "--graph-risk-gap-weight 0.5 "
    "--graph-risk-triangle-weight 0.0 "
    "--graph-risk-unilateral-weight 0.0 "
    "--refine-mode bridge-cut "
    "--refine-confidence-threshold 0.55 "
    "--refine-bridge-ratio 0.7"
)

DEFAULT_EXISTING_RUNS = [
    ("dev_core_historical", "benchmark/runs/20260419T211954Z-a-dev_core"),
    ("dev_full_support068", "benchmark/runs/20260420T161714Z-a-dev_full-support068"),
    ("holdout_current", "benchmark/runs/20260421T-confidence-holdout"),
]

DEFAULT_SPLITS = [
    ("dev_core", "benchmark/splits/dev_core.txt"),
    ("dev_full", "benchmark/splits/dev_full.txt"),
    ("holdout", "benchmark/splits/holdout.txt"),
]


@dataclass
class ComparisonRow:
    dataset: str
    lects: int
    forms: int
    cognator_p: float
    cognator_r: float
    cognator_f: float
    cognator_ms: int
    lingpy_p: float
    lingpy_r: float
    lingpy_f: float
    lingpy_ms: int
    delta_f: float
    cognator_skipped: str
    lingpy_skipped: str


@dataclass
class ErrorRow:
    dataset: str
    concepts: int
    forms: int
    pair_tp: int
    pair_fp: int
    pair_fn: int
    false_split_rate_mean: float
    false_merge_rate_mean: float
    false_split_rate_global: float
    false_merge_rate_global: float


@dataclass
class RunReport:
    label: str
    run_dir: Path
    comparison_rows: list[ComparisonRow]
    cognator_errors: dict[str, ErrorRow]
    lingpy_errors: dict[str, ErrorRow]
    meta: dict[str, object]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode",
        choices=("existing", "fresh"),
        default="existing",
        help="Read existing run dirs or run fresh benchmarks before reporting",
    )
    parser.add_argument(
        "--run-dir",
        action="append",
        default=[],
        metavar="LABEL=PATH",
        help="Existing run dir containing comparison.tsv; repeatable. Defaults to curated local runs.",
    )
    parser.add_argument(
        "--split",
        action="append",
        default=[],
        metavar="LABEL=PATH",
        help="Fresh-mode split file; repeatable. Defaults to dev_core/dev_full/holdout.",
    )
    parser.add_argument(
        "--forms",
        default="",
        help="arcaverborum forms.csv/forms.tsv. Fresh mode only; inferred from --arcaverborum-root if omitted.",
    )
    parser.add_argument(
        "--arcaverborum-root",
        default="../arcaverborum",
        help="arcaverborum repository root for fresh mode",
    )
    parser.add_argument(
        "--collection",
        choices=("full", "core", "corecog"),
        default="corecog",
        help="arcaverborum built collection for fresh mode",
    )
    parser.add_argument(
        "--datasets-csv",
        default="../arcaverborum/datasets.csv",
        help="arcaverborum datasets.csv",
    )
    parser.add_argument(
        "--merkmal-dir",
        default="tests/fixtures/merkmal-models",
        help="Merkmal data root used by Cognator",
    )
    parser.add_argument(
        "--track",
        choices=("A", "B"),
        default="A",
        help="Benchmark track",
    )
    parser.add_argument(
        "--out-dir",
        default="benchmark/reports/arcaverborum",
        help="Directory for the generated report and fresh run artifacts",
    )
    parser.add_argument(
        "--report-md",
        default="",
        help="Markdown report path; defaults under --out-dir",
    )
    parser.add_argument(
        "--summary-tsv",
        default="",
        help="Aggregate summary TSV path; defaults under --out-dir",
    )
    parser.add_argument(
        "--cognator-extra-args",
        default=CURRENT_COGNATOR_ARGS,
        help="Fresh-mode extra args passed to scripts/run_cognator_eval_table.py",
    )
    parser.add_argument(
        "--lingpy-method",
        choices=("lexstat", "sca"),
        default="lexstat",
        help="LingPy scorer for fresh mode",
    )
    parser.add_argument(
        "--lingpy-cluster-method",
        choices=("infomap", "upgma", "mcl", "single", "complete"),
        default="infomap",
        help="LingPy clustering method for fresh mode",
    )
    parser.add_argument(
        "--lingpy-threshold",
        type=float,
        default=0.55,
        help="LingPy clustering threshold for fresh mode",
    )
    parser.add_argument(
        "--lingpy-runs",
        type=int,
        default=1000,
        help="LingPy LexStat permutation runs for fresh mode",
    )
    parser.add_argument(
        "--parallel-families",
        type=int,
        default=2,
        help="Cognator concurrent families in fresh mode",
    )
    parser.add_argument(
        "--cognator-jobs",
        type=int,
        default=0,
        help="Cognator per-family jobs in fresh mode",
    )
    parser.add_argument(
        "--skip-error-reports",
        action="store_true",
        help="Fresh mode: skip pairwise error reports",
    )
    return parser.parse_args()


def parse_label_path(items: list[str], defaults: list[tuple[str, str]]) -> list[tuple[str, Path]]:
    raw = items or [f"{label}={path}" for label, path in defaults]
    out: list[tuple[str, Path]] = []
    for item in raw:
        if "=" not in item:
            raise SystemExit(f"expected LABEL=PATH, got {item!r}")
        label, path = item.split("=", 1)
        label = label.strip()
        if not label:
            raise SystemExit(f"empty label in {item!r}")
        out.append((label, Path(path)))
    return out


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(shlex.quote(part) for part in cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def resolve_forms_path(args: argparse.Namespace) -> Path:
    if args.forms:
        return Path(args.forms)
    candidate = Path(args.arcaverborum_root) / "output" / args.collection / "forms.csv"
    if candidate.exists():
        return candidate
    raise SystemExit(
        f"could not find {candidate}; pass --forms or build arcaverborum collection first"
    )


def fresh_runs(args: argparse.Namespace) -> list[tuple[str, Path]]:
    out_root = Path(args.out_dir)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_root = out_root / "runs" / timestamp
    export_dir = run_root / "export"
    run_root.mkdir(parents=True, exist_ok=True)

    forms = resolve_forms_path(args)
    export_cmd = [
        "python",
        "scripts/export_arcaverborum_eval.py",
        "--forms",
        str(forms),
        "--datasets-csv",
        args.datasets_csv,
        "--out-dir",
        str(export_dir),
    ]
    run(export_cmd)
    eval_table = export_dir / ("track_a.tsv" if args.track == "A" else "track_b.tsv")
    if not eval_table.exists():
        raise SystemExit(f"expected exported eval table at {eval_table}")

    runs: list[tuple[str, Path]] = []

    for label, split_path in parse_label_path(args.split, DEFAULT_SPLITS):
        run_dir = run_root / label
        run_dir.mkdir(parents=True, exist_ok=True)
        run_label = f"{args.track.lower()}-{label}"

        cognator_results = run_dir / "cognator_results.tsv"
        cognator_md = run_dir / "cognator_results.md"
        cognator_preds = run_dir / "cognator_predictions.tsv"
        bench_cmd = [
            "python",
            "scripts/run_cognator_eval_table.py",
            "--eval-table",
            str(eval_table),
            "--split-file",
            str(split_path),
            "--track",
            args.track,
            "--merkmal-dir",
            args.merkmal_dir,
            "--out-tsv",
            str(cognator_results),
            "--out-md",
            str(cognator_md),
            "--run-label",
            run_label,
            "--parallel-families",
            str(args.parallel_families),
        ]
        if args.cognator_jobs:
            bench_cmd.extend(["--jobs", str(args.cognator_jobs)])
        if args.cognator_extra_args:
            bench_cmd.extend(shlex.split(args.cognator_extra_args))
        run(bench_cmd)

        lingpy_results = run_dir / "lingpy_results.tsv"
        lingpy_preds = run_dir / "lingpy_predictions.tsv"
        lingpy_cmd = [
            "python",
            "scripts/run_lingpy_baseline.py",
            "--eval-table",
            str(eval_table),
            "--split-file",
            str(split_path),
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
        run(lingpy_cmd)

        run(
            [
                "python",
                "scripts/compare_benchmark_results.py",
                "--cognator",
                str(cognator_results),
                "--lingpy",
                str(lingpy_results),
                "--out-tsv",
                str(run_dir / "comparison.tsv"),
                "--out-md",
                str(run_dir / "comparison.md"),
            ]
        )

        if not args.skip_error_reports:
            if cognator_preds.exists():
                write_error_reports(cognator_preds, run_dir, "cognator")
            write_error_reports(lingpy_preds, run_dir, "lingpy")

        meta = {
            "timestamp_utc": timestamp,
            "run_label": run_label,
            "forms": str(forms),
            "datasets_csv": args.datasets_csv,
            "merkmal_dir": args.merkmal_dir,
            "split_file": str(split_path),
            "track": args.track,
            "eval_table": str(eval_table),
            "cognator_extra_args": args.cognator_extra_args,
            "lingpy": {
                "method": args.lingpy_method,
                "cluster_method": args.lingpy_cluster_method,
                "threshold": args.lingpy_threshold,
                "runs": args.lingpy_runs,
            },
        }
        (run_dir / "meta.json").write_text(
            json.dumps(meta, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        runs.append((label, run_dir))

    return runs


def write_error_reports(predictions: Path, run_dir: Path, label: str) -> None:
    run(
        [
            "python",
            "scripts/error_report.py",
            "--predictions",
            str(predictions),
            "--out-tsv",
            str(run_dir / f"{label}_errors.tsv"),
            "--out-dataset-tsv",
            str(run_dir / f"{label}_error_summary.tsv"),
            "--out-md",
            str(run_dir / f"{label}_error_summary.md"),
            "--label",
            label,
        ]
    )


def load_meta(run_dir: Path) -> dict[str, object]:
    path = run_dir / "meta.json"
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def load_comparison(path: Path) -> list[ComparisonRow]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        return [
            ComparisonRow(
                dataset=row["dataset"],
                lects=int(row["lects"] or 0),
                forms=int(row["forms"] or 0),
                cognator_p=float(row["cognator_P"] or 0),
                cognator_r=float(row["cognator_R"] or 0),
                cognator_f=float(row["cognator_F"] or 0),
                cognator_ms=int(float(row["cognator_ms"] or 0)),
                lingpy_p=float(row["lingpy_P"] or 0),
                lingpy_r=float(row["lingpy_R"] or 0),
                lingpy_f=float(row["lingpy_F"] or 0),
                lingpy_ms=int(float(row["lingpy_ms"] or 0)),
                delta_f=float(row["delta_F"] or 0),
                cognator_skipped=row.get("cognator_skipped", ""),
                lingpy_skipped=row.get("lingpy_skipped", ""),
            )
            for row in reader
        ]


def load_error_summary(path: Path) -> dict[str, ErrorRow]:
    if not path.exists():
        return {}
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        out: dict[str, ErrorRow] = {}
        for row in reader:
            out[row["dataset"]] = ErrorRow(
                dataset=row["dataset"],
                concepts=int(row["concepts"] or 0),
                forms=int(row["forms"] or 0),
                pair_tp=int(row["pair_tp"] or 0),
                pair_fp=int(row["pair_fp"] or 0),
                pair_fn=int(row["pair_fn"] or 0),
                false_split_rate_mean=float(row["false_split_rate_mean"] or 0),
                false_merge_rate_mean=float(row["false_merge_rate_mean"] or 0),
                false_split_rate_global=float(row["false_split_rate_global"] or 0),
                false_merge_rate_global=float(row["false_merge_rate_global"] or 0),
            )
        return out


def load_run(label: str, run_dir: Path) -> RunReport:
    comparison = run_dir / "comparison.tsv"
    if not comparison.exists():
        raise SystemExit(f"{run_dir} has no comparison.tsv")
    return RunReport(
        label=label,
        run_dir=run_dir,
        comparison_rows=load_comparison(comparison),
        cognator_errors=load_error_summary(run_dir / "cognator_error_summary.tsv"),
        lingpy_errors=load_error_summary(run_dir / "lingpy_error_summary.tsv"),
        meta=load_meta(run_dir),
    )


def completed(rows: list[ComparisonRow]) -> list[ComparisonRow]:
    return [
        row
        for row in rows
        if not row.cognator_skipped
        and not row.lingpy_skipped
        and row.cognator_f > 0
        and row.lingpy_f > 0
    ]


def weighted_mean(rows: list[ComparisonRow], attr: str) -> float:
    total = sum(row.forms for row in rows)
    if total <= 0:
        return 0.0
    return sum(getattr(row, attr) * row.forms for row in rows) / total


def avg(values: list[float]) -> float:
    return mean(values) if values else 0.0


def fmt_float(value: float, digits: int = 4) -> str:
    return f"{value:.{digits}f}"


def fmt_ms(ms: int | float) -> str:
    ms = int(ms)
    if ms < 1000:
        return f"{ms}ms"
    seconds = ms / 1000
    if seconds < 60:
        return f"{seconds:.1f}s"
    minutes = int(seconds // 60)
    rem = int(seconds % 60)
    return f"{minutes}m{rem:02d}s"


def aggregate(run: RunReport) -> dict[str, float | int | str]:
    rows = completed(run.comparison_rows)
    wins = sum(row.delta_f > 1e-6 for row in rows)
    losses = sum(row.delta_f < -1e-6 for row in rows)
    ties = len(rows) - wins - losses
    cog_ms = sum(row.cognator_ms for row in rows)
    lpy_ms = sum(row.lingpy_ms for row in rows)
    return {
        "label": run.label,
        "datasets": len(rows),
        "cognator_f": avg([row.cognator_f for row in rows]),
        "lingpy_f": avg([row.lingpy_f for row in rows]),
        "delta_f": avg([row.delta_f for row in rows]),
        "cognator_p": avg([row.cognator_p for row in rows]),
        "lingpy_p": avg([row.lingpy_p for row in rows]),
        "delta_p": avg([row.cognator_p - row.lingpy_p for row in rows]),
        "cognator_r": avg([row.cognator_r for row in rows]),
        "lingpy_r": avg([row.lingpy_r for row in rows]),
        "delta_r": avg([row.cognator_r - row.lingpy_r for row in rows]),
        "weighted_cognator_f": weighted_mean(rows, "cognator_f"),
        "weighted_lingpy_f": weighted_mean(rows, "lingpy_f"),
        "weighted_delta_f": weighted_mean(rows, "delta_f"),
        "cognator_mean_ms": int(cog_ms / len(rows)) if rows else 0,
        "lingpy_mean_ms": int(lpy_ms / len(rows)) if rows else 0,
        "speedup": (lpy_ms / cog_ms) if cog_ms > 0 else 0.0,
        "wins": wins,
        "losses": losses,
        "ties": ties,
        "median_delta_f": median([row.delta_f for row in rows]) if rows else 0.0,
    }


def error_aggregate(errors: dict[str, ErrorRow]) -> dict[str, float]:
    rows = list(errors.values())
    if not rows:
        return {}
    total_tp = sum(row.pair_tp for row in rows)
    total_fp = sum(row.pair_fp for row in rows)
    total_fn = sum(row.pair_fn for row in rows)
    return {
        "false_merge_global": total_fp / (total_tp + total_fp) if total_tp + total_fp else 0.0,
        "false_split_global": total_fn / (total_tp + total_fn) if total_tp + total_fn else 0.0,
        "false_merge_mean": avg([row.false_merge_rate_mean for row in rows]),
        "false_split_mean": avg([row.false_split_rate_mean for row in rows]),
    }


def write_summary_tsv(path: Path, runs: list[RunReport]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "label",
        "datasets",
        "cognator_f",
        "lingpy_f",
        "delta_f",
        "cognator_p",
        "lingpy_p",
        "delta_p",
        "cognator_r",
        "lingpy_r",
        "delta_r",
        "weighted_cognator_f",
        "weighted_lingpy_f",
        "weighted_delta_f",
        "wins",
        "losses",
        "ties",
        "cognator_mean_ms",
        "lingpy_mean_ms",
        "speedup",
        "median_delta_f",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        for run_report in runs:
            writer.writerow(aggregate(run_report))


def write_report_md(path: Path, runs: list[RunReport], args: argparse.Namespace) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# Cognator vs LingPy on arcaverborum\n\n")
        handle.write(f"Generated: `{generated}`\n\n")
        handle.write("This report compares Cognator against LingPy `LexStat + Infomap` using benchmark artifacts generated from arcaverborum-style evaluation tables. Scores are B-cubed precision, recall, and F1 over same-concept cognate classes.\n\n")
        handle.write("## Configuration\n\n")
        handle.write(f"- Report mode: `{args.mode}`\n")
        handle.write(f"- Track: `{args.track}`\n")
        handle.write(f"- Cognator args for fresh runs: `{args.cognator_extra_args}`\n")
        handle.write(f"- LingPy method for fresh runs: `{args.lingpy_method}`\n")
        handle.write(f"- LingPy cluster method for fresh runs: `{args.lingpy_cluster_method}`\n")
        handle.write(f"- LingPy threshold for fresh runs: `{args.lingpy_threshold}`\n")
        handle.write(f"- LingPy runs for fresh runs: `{args.lingpy_runs}`\n\n")

        handle.write("## Aggregate Results\n\n")
        handle.write("| Split/run | N | Cognator F | LingPy F | Delta F | Cognator P/R | LingPy P/R | Wins/Losses/Ties | Mean time | Speedup |\n")
        handle.write("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")
        for run_report in runs:
            agg = aggregate(run_report)
            handle.write(
                f"| {run_report.label} | {agg['datasets']} | "
                f"{agg['cognator_f']:.4f} | {agg['lingpy_f']:.4f} | {agg['delta_f']:+.4f} | "
                f"{agg['cognator_p']:.4f}/{agg['cognator_r']:.4f} | "
                f"{agg['lingpy_p']:.4f}/{agg['lingpy_r']:.4f} | "
                f"{agg['wins']}/{agg['losses']}/{agg['ties']} | "
                f"{fmt_ms(int(agg['cognator_mean_ms']))} vs {fmt_ms(int(agg['lingpy_mean_ms']))} | "
                f"{agg['speedup']:.1f}x |\n"
            )

        handle.write("\n## Form-Weighted Results\n\n")
        handle.write("| Split/run | Cognator weighted F | LingPy weighted F | Delta weighted F | Median family delta F |\n")
        handle.write("|---|---:|---:|---:|---:|\n")
        for run_report in runs:
            agg = aggregate(run_report)
            handle.write(
                f"| {run_report.label} | {agg['weighted_cognator_f']:.4f} | "
                f"{agg['weighted_lingpy_f']:.4f} | {agg['weighted_delta_f']:+.4f} | "
                f"{agg['median_delta_f']:+.4f} |\n"
            )

        for run_report in runs:
            write_run_section(handle, run_report)

        handle.write("\n## Interpretation\n\n")
        handle.write("- Cognator is consistently much faster than LingPy on the measured arcaverborum splits.\n")
        handle.write("- Cognator's recall is often competitive or better, but precision losses identify merge-sensitive families as the main remaining risk.\n")
        handle.write("- The current support/adaptive/bridge-cut baseline is strong enough for research iteration, but holdout results show it does not yet consistently beat LingPy.\n")
        handle.write("- The next useful reports should add confidence calibration curves and a family spot-check table for the largest negative deltas.\n")


def write_run_section(handle, run_report: RunReport) -> None:
    rows = completed(run_report.comparison_rows)
    rows_by_delta = sorted(rows, key=lambda row: row.delta_f, reverse=True)
    handle.write(f"\n## {run_report.label}\n\n")
    handle.write(f"Run directory: `{run_report.run_dir}`\n\n")
    if run_report.meta:
        split_file = run_report.meta.get("split_file", "")
        run_label = run_report.meta.get("run_label", "")
        cog = run_report.meta.get("cognator", {})
        if isinstance(cog, dict):
            cog_args = cog.get("extra_args", "")
        else:
            cog_args = run_report.meta.get("cognator_extra_args", "")
        handle.write(f"- Run label: `{run_label}`\n")
        handle.write(f"- Split file: `{split_file}`\n")
        if cog_args:
            handle.write(f"- Cognator run args: `{cog_args}`\n")
        handle.write("\n")

    handle.write("### Per-Dataset Scores\n\n")
    handle.write("| Dataset | Forms | Cognator F | LingPy F | Delta F | Cognator P/R | LingPy P/R | Time |\n")
    handle.write("|---|---:|---:|---:|---:|---:|---:|---:|\n")
    for row in rows_by_delta:
        handle.write(
            f"| {row.dataset} | {row.forms} | {row.cognator_f:.4f} | {row.lingpy_f:.4f} | "
            f"{row.delta_f:+.4f} | {row.cognator_p:.4f}/{row.cognator_r:.4f} | "
            f"{row.lingpy_p:.4f}/{row.lingpy_r:.4f} | "
            f"{fmt_ms(row.cognator_ms)} vs {fmt_ms(row.lingpy_ms)} |\n"
        )

    top_wins = [row for row in rows_by_delta if row.delta_f > 0][:5]
    top_losses = [row for row in sorted(rows, key=lambda row: row.delta_f) if row.delta_f < 0][:5]
    handle.write("\n### Largest Cognator Wins\n\n")
    write_delta_list(handle, top_wins)
    handle.write("\n### Largest Cognator Losses\n\n")
    write_delta_list(handle, top_losses)

    cog_err = error_aggregate(run_report.cognator_errors)
    lpy_err = error_aggregate(run_report.lingpy_errors)
    if cog_err and lpy_err:
        handle.write("\n### Pairwise Error Decomposition\n\n")
        handle.write("| System | False merge global | False split global | False merge mean | False split mean |\n")
        handle.write("|---|---:|---:|---:|---:|\n")
        handle.write(
            f"| Cognator | {cog_err['false_merge_global']:.4f} | {cog_err['false_split_global']:.4f} | "
            f"{cog_err['false_merge_mean']:.4f} | {cog_err['false_split_mean']:.4f} |\n"
        )
        handle.write(
            f"| LingPy | {lpy_err['false_merge_global']:.4f} | {lpy_err['false_split_global']:.4f} | "
            f"{lpy_err['false_merge_mean']:.4f} | {lpy_err['false_split_mean']:.4f} |\n"
        )
    else:
        handle.write("\nPairwise error summaries were not available for both systems in this run directory.\n")

    skipped = [
        row
        for row in run_report.comparison_rows
        if row.cognator_skipped or row.lingpy_skipped or row.cognator_f == 0 or row.lingpy_f == 0
    ]
    if skipped:
        handle.write("\n### Skipped Or Incomplete Datasets\n\n")
        handle.write("| Dataset | Cognator | LingPy |\n")
        handle.write("|---|---|---|\n")
        for row in skipped:
            c_status = row.cognator_skipped or (f"F={row.cognator_f:.4f}" if row.cognator_f else "missing")
            l_status = row.lingpy_skipped or (f"F={row.lingpy_f:.4f}" if row.lingpy_f else "missing")
            handle.write(f"| {row.dataset} | {c_status} | {l_status} |\n")


def write_delta_list(handle, rows: list[ComparisonRow]) -> None:
    if not rows:
        handle.write("No rows.\n")
        return
    handle.write("| Dataset | Delta F | Cognator F | LingPy F | Forms |\n")
    handle.write("|---|---:|---:|---:|---:|\n")
    for row in rows:
        handle.write(
            f"| {row.dataset} | {row.delta_f:+.4f} | {row.cognator_f:.4f} | "
            f"{row.lingpy_f:.4f} | {row.forms} |\n"
        )


def main() -> None:
    args = parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    report_md = Path(args.report_md) if args.report_md else out_dir / "arcaverborum_performance_report.md"
    summary_tsv = Path(args.summary_tsv) if args.summary_tsv else out_dir / "arcaverborum_performance_summary.tsv"

    if args.mode == "fresh":
        run_specs = fresh_runs(args)
    else:
        run_specs = parse_label_path(args.run_dir, DEFAULT_EXISTING_RUNS)

    runs = [load_run(label, run_dir) for label, run_dir in run_specs]
    write_summary_tsv(summary_tsv, runs)
    write_report_md(report_md, runs, args)
    print(f"wrote {report_md}")
    print(f"wrote {summary_tsv}")


if __name__ == "__main__":
    main()
