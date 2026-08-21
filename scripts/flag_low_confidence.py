#!/usr/bin/env python3
"""Rank low-confidence prediction rows for manual review.

The input is the prediction TSV emitted by `scripts/run_cognator_eval_table.py --pred-out`. The script
does not change clustering decisions; it produces a review queue from existing
confidence diagnostics. When gold labels are present, rows are annotated with
pairwise false-merge / false-split involvement for evaluating review quality.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from itertools import combinations
from pathlib import Path

REQUIRED_COLUMNS = {
    "eval_id",
    "dataset",
    "concept",
    "gold",
    "pred",
    "confidence",
    "assigned_support",
    "alternative_support",
    "support_margin",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, help="Prediction TSV from scripts/run_cognator_eval_table.py --pred-out")
    parser.add_argument("--out-tsv", required=True, help="Ranked output TSV")
    parser.add_argument("--out-md", default="", help="Optional markdown summary")
    parser.add_argument("--top", type=int, default=200, help="Rows to include in markdown top-list")
    parser.add_argument(
        "--confidence-threshold",
        type=float,
        default=0.77,
        help="Flag rows with confidence at or below this value",
    )
    parser.add_argument(
        "--alternative-support-threshold",
        type=float,
        default=0.26,
        help="Flag rows with alternative support at or above this value",
    )
    parser.add_argument(
        "--support-margin-threshold",
        type=float,
        default=0.0,
        help="Flag rows with support margin at or below this value",
    )
    return parser.parse_args()


def read_predictions(path: str) -> list[dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        missing = REQUIRED_COLUMNS - set(reader.fieldnames or ())
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


def row_flags(
    row: dict[str, str],
    confidence_threshold: float,
    alternative_support_threshold: float,
    support_margin_threshold: float,
) -> list[str]:
    flags: list[str] = []
    if float(row["confidence"]) <= confidence_threshold:
        flags.append("low_confidence")
    if float(row["alternative_support"]) >= alternative_support_threshold:
        flags.append("high_alternative_support")
    if float(row["support_margin"]) <= support_margin_threshold:
        flags.append("low_support_margin")
    return flags


def ranked_rows(rows: list[dict[str, str]], involvement: dict[str, set[str]], args: argparse.Namespace) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for row in rows:
        flags = row_flags(
            row,
            args.confidence_threshold,
            args.alternative_support_threshold,
            args.support_margin_threshold,
        )
        if not flags:
            continue
        eval_id = row["eval_id"]
        out.append(
            {
                "rank": 0,
                "dataset": row["dataset"],
                "concept": row["concept"],
                "eval_id": eval_id,
                "gold": row["gold"],
                "pred": row["pred"],
                "confidence": float(row["confidence"]),
                "assigned_support": float(row["assigned_support"]),
                "alternative_support": float(row["alternative_support"]),
                "support_margin": float(row["support_margin"]),
                "flags": ",".join(flags),
                "flag_count": len(flags),
                "error_involved": int(eval_id in involvement["error_involved"]),
                "false_merge_involved": int(eval_id in involvement["false_merge_involved"]),
                "false_split_involved": int(eval_id in involvement["false_split_involved"]),
                "run_label": row.get("run_label", ""),
            }
        )

    out.sort(
        key=lambda row: (
            -int(row["flag_count"]),
            float(row["confidence"]),
            -float(row["alternative_support"]),
            float(row["support_margin"]),
            str(row["dataset"]),
            str(row["concept"]),
            str(row["eval_id"]),
        )
    )
    for index, row in enumerate(out, start=1):
        row["rank"] = index
    return out


def write_tsv(path: str, rows: list[dict[str, object]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "rank",
        "dataset",
        "concept",
        "eval_id",
        "gold",
        "pred",
        "confidence",
        "assigned_support",
        "alternative_support",
        "support_margin",
        "flags",
        "flag_count",
        "error_involved",
        "false_merge_involved",
        "false_split_involved",
        "run_label",
    ]
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def error_rate(rows: list[dict[str, object]]) -> float:
    if not rows:
        return 0.0
    return sum(int(row["error_involved"]) for row in rows) / len(rows)


def write_md(path: str, rows: list[dict[str, object]], args: argparse.Namespace) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    top = rows[: args.top]
    dataset_counts = Counter(str(row["dataset"]) for row in top)
    flag_counts = Counter(flag for row in top for flag in str(row["flags"]).split(",") if flag)

    with Path(path).open("w", encoding="utf-8") as handle:
        handle.write("# Low-Confidence Review Queue\n\n")
        handle.write(
            "Thresholds: "
            f"confidence <= {args.confidence_threshold:.4f}, "
            f"alternative_support >= {args.alternative_support_threshold:.4f}, "
            f"support_margin <= {args.support_margin_threshold:.4f}.\n\n"
        )
        handle.write(f"Flagged rows: {len(rows)}\n\n")
        for size in (50, 100, 200):
            subset = rows[:size]
            if subset:
                handle.write(f"Top {len(subset)} error-involved share: {error_rate(subset):.4f}\n\n")
        if top:
            handle.write(f"Top {len(top)} dataset counts: ")
            handle.write(", ".join(f"{dataset}={count}" for dataset, count in dataset_counts.most_common()))
            handle.write("\n\n")
            handle.write(f"Top {len(top)} flag counts: ")
            handle.write(", ".join(f"{flag}={count}" for flag, count in flag_counts.most_common()))
            handle.write("\n\n")
            handle.write("| Rank | Dataset | Concept | Confidence | Alternative support | Margin | Flags | Error |\n")
            handle.write("|---:|---|---|---:|---:|---:|---|---:|\n")
            for row in top:
                handle.write(
                    f"| {row['rank']} | {row['dataset']} | {row['concept']} | "
                    f"{row['confidence']:.4f} | {row['alternative_support']:.4f} | "
                    f"{row['support_margin']:.4f} | {row['flags']} | {row['error_involved']} |\n"
                )


def main() -> None:
    args = parse_args()
    rows = read_predictions(args.predictions)
    involvement = error_involvement(rows)
    flagged = ranked_rows(rows, involvement, args)
    write_tsv(args.out_tsv, flagged)
    if args.out_md:
        write_md(args.out_md, flagged, args)


if __name__ == "__main__":
    main()
