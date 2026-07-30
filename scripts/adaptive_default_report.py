#!/usr/bin/env python3
"""Summarize adaptive-default validation against LingPy comparison artifacts."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from statistics import mean


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--comparison", default="benchmark/comparison.tsv")
    parser.add_argument("--parameter-summary", default="benchmark/milestone24/parameter_summary.tsv")
    parser.add_argument("--out-md", default="docs/adaptive_default_validation.md")
    return parser.parse_args()


def read_tsv(path: str) -> list[dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def number(row: dict[str, str], field: str) -> float:
    try:
        return float((row.get(field) or "0").replace("+", ""))
    except ValueError:
        return 0.0


def runnable(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [
        row
        for row in rows
        if not row.get("cognator_skipped") and not row.get("lingpy_skipped") and row.get("dataset")
    ]


def threshold_summary(rows: list[dict[str, str]]) -> list[str]:
    out = []
    for row in rows:
        if row.get("scope") == "cognator_threshold":
            out.append(
                f"- `{row['selector']}`: mean F1 `{number(row, 'mean_f1'):.4f}`, "
                f"best count `{int(number(row, 'best_count'))}`, mean rank `{number(row, 'mean_rank'):.2f}`."
            )
    return out


def table(rows: list[dict[str, str]]) -> list[str]:
    lines = ["| Dataset | Forms | Cognator F1 | LingPy F1 | Delta F1 |", "|---|---:|---:|---:|---:|"]
    for row in rows:
        lines.append(
            f"| {row['dataset']} | {row['forms']} | {number(row, 'cognator_F'):.4f} | "
            f"{number(row, 'lingpy_F'):.4f} | {number(row, 'delta_F'):+.4f} |"
        )
    return lines


def main() -> None:
    args = parse_args()
    comparison_path = Path(args.comparison)
    parameter_path = Path(args.parameter_summary)
    rows = runnable(read_tsv(str(comparison_path)))
    if not rows:
        raise SystemExit(f"no runnable comparison rows in {comparison_path}")

    wins = [row for row in rows if number(row, "delta_F") > 0.0005]
    losses = [row for row in rows if number(row, "delta_F") < -0.0005]
    ties = len(rows) - len(wins) - len(losses)
    sorted_rows = sorted(rows, key=lambda row: number(row, "delta_F"), reverse=True)
    mean_c = mean(number(row, "cognator_F") for row in rows)
    mean_l = mean(number(row, "lingpy_F") for row in rows)
    mean_delta = mean(number(row, "delta_F") for row in rows)
    speedups = [
        number(row, "lingpy_ms") / number(row, "cognator_ms")
        for row in rows
        if number(row, "cognator_ms") > 0 and number(row, "lingpy_ms") > 0
    ]

    lines = [
        "# Adaptive Default Validation",
        "",
        "This report records the current wider-sample evidence used to validate the C/Python adaptive default against LingPy.",
        "",
        "## Evidence",
        "",
        f"- Comparison source: `{comparison_path}`.",
        f"- Parameter-grid source: `{parameter_path}`.",
        "- Cognator is evaluated through the C/Python path; LingPy is the external quality target.",
        "- The committed comparison artifact is the authoritative data for this report; rerun the generator after producing fresh benchmark TSVs.",
        "",
        "## Aggregate",
        "",
        f"- Runnable paired datasets: `{len(rows)}`.",
        f"- Mean Cognator F1: `{mean_c:.4f}`.",
        f"- Mean LingPy F1: `{mean_l:.4f}`.",
        f"- Mean delta F1: `{mean_delta:+.4f}`.",
        f"- Win/loss/tie count: `{len(wins)}/{len(losses)}/{ties}`.",
        f"- Mean runtime speedup where both times are nonzero: `{mean(speedups):.1f}x`." if speedups else "- Mean runtime speedup: unavailable.",
        "",
        "## Threshold Findings",
        "",
    ]
    if parameter_path.exists():
        lines.extend(threshold_summary(read_tsv(str(parameter_path))))
    else:
        lines.append("- Parameter-grid source is missing, so threshold distribution was not summarized.")
    lines.extend(
        [
            "",
            "Interpretation: the best Cognator settings in the grid concentrate in the lower threshold band, which is why the adaptive preset clamps family-level Otsu thresholds to `0.64..0.74` and leaves concept-local shrinkage as an explicit experiment knob.",
            "",
            "## Strongest Wins",
            "",
        ]
    )
    lines.extend(table(sorted_rows[:10]))
    lines.extend(["", "## Largest Regressions", ""])
    lines.extend(table(list(reversed(sorted_rows[-10:]))))
    lines.extend(
        [
            "",
            "## Conclusion",
            "",
            "The adaptive C/Python default is fast and competitive on several families, but it is not yet strong enough to claim consistent quality superiority over LingPy; the next modeling work should target the largest false-merge and false-split regressions without sacrificing the runtime advantage.",
            "",
        ]
    )
    Path(args.out_md).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out_md).write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
