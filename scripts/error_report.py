#!/usr/bin/env python3
"""Compute pairwise error decomposition from prediction TSVs.

The script expects the prediction format emitted by:
- `go run ./cmd/bench --pred-out ...`
- `python scripts/run_lingpy_baseline.py --pred-out ...`

It writes:
- a per-concept TSV with pairwise TP/FP/FN counts
- a per-dataset summary TSV
- an optional markdown summary
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from itertools import combinations
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, help="Prediction TSV")
    parser.add_argument("--out-tsv", required=True, help="Per-concept error TSV")
    parser.add_argument("--out-dataset-tsv", required=True, help="Per-dataset summary TSV")
    parser.add_argument("--out-md", default="", help="Optional markdown summary")
    parser.add_argument("--label", default="", help="Optional system label override")
    return parser.parse_args()


def read_predictions(path: str) -> list[dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        required = {"eval_id", "dataset", "concept", "gold", "pred"}
        missing = required - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"missing required columns: {sorted(missing)}")
        return list(reader)


def pair_counts(rows: list[dict[str, str]]) -> tuple[int, int, int]:
    tp = fp = fn = 0
    for left, right in combinations(rows, 2):
        gold_same = left["gold"] == right["gold"]
        pred_same = left["pred"] == right["pred"]
        if gold_same and pred_same:
            tp += 1
        elif gold_same and not pred_same:
            fn += 1
        elif not gold_same and pred_same:
            fp += 1
    return tp, fp, fn


def build_reports(rows: list[dict[str, str]]) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    by_concept: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        by_concept[(row["dataset"], row["concept"])].append(row)

    concept_rows: list[dict[str, object]] = []
    dataset_acc: dict[str, dict[str, float]] = defaultdict(lambda: defaultdict(float))
    dataset_counts: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))

    for (dataset, concept), members in sorted(by_concept.items()):
        forms = len(members)
        gold_clusters = len({row["gold"] for row in members})
        pred_clusters = len({row["pred"] for row in members})
        tp, fp, fn = pair_counts(members)
        false_split_rate = fn / (tp + fn) if (tp + fn) else 0.0
        false_merge_rate = fp / (tp + fp) if (tp + fp) else 0.0
        concept_rows.append(
            {
                "dataset": dataset,
                "concept": concept,
                "forms": forms,
                "gold_clusters": gold_clusters,
                "pred_clusters": pred_clusters,
                "pair_tp": tp,
                "pair_fp": fp,
                "pair_fn": fn,
                "false_split_rate": false_split_rate,
                "false_merge_rate": false_merge_rate,
            }
        )
        dataset_acc[dataset]["forms"] += forms
        dataset_acc[dataset]["pair_tp"] += tp
        dataset_acc[dataset]["pair_fp"] += fp
        dataset_acc[dataset]["pair_fn"] += fn
        dataset_acc[dataset]["sum_false_split_rate"] += false_split_rate
        dataset_acc[dataset]["sum_false_merge_rate"] += false_merge_rate
        dataset_counts[dataset]["concepts"] += 1

    dataset_rows: list[dict[str, object]] = []
    for dataset in sorted(dataset_acc):
        acc = dataset_acc[dataset]
        n_concepts = dataset_counts[dataset]["concepts"]
        pair_tp = int(acc["pair_tp"])
        pair_fp = int(acc["pair_fp"])
        pair_fn = int(acc["pair_fn"])
        dataset_rows.append(
            {
                "dataset": dataset,
                "concepts": n_concepts,
                "forms": int(acc["forms"]),
                "pair_tp": pair_tp,
                "pair_fp": pair_fp,
                "pair_fn": pair_fn,
                "false_split_rate_mean": acc["sum_false_split_rate"] / n_concepts if n_concepts else 0.0,
                "false_merge_rate_mean": acc["sum_false_merge_rate"] / n_concepts if n_concepts else 0.0,
                "false_split_rate_global": pair_fn / (pair_tp + pair_fn) if (pair_tp + pair_fn) else 0.0,
                "false_merge_rate_global": pair_fp / (pair_tp + pair_fp) if (pair_tp + pair_fp) else 0.0,
            }
        )
    return concept_rows, dataset_rows


def write_tsv(path: str, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_md(path: str, label: str, dataset_rows: list[dict[str, object]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("w", encoding="utf-8") as handle:
        handle.write(f"# Error Report: {label}\n\n")
        handle.write("| Dataset | Concepts | Forms | Global False Split | Global False Merge | Mean False Split | Mean False Merge |\n")
        handle.write("|---|---|---|---|---|---|---|\n")
        for row in sorted(dataset_rows, key=lambda item: item["false_split_rate_global"], reverse=True):
            handle.write(
                f"| {row['dataset']} | {row['concepts']} | {row['forms']} | "
                f"{row['false_split_rate_global']:.3f} | {row['false_merge_rate_global']:.3f} | "
                f"{row['false_split_rate_mean']:.3f} | {row['false_merge_rate_mean']:.3f} |\n"
            )


def infer_label(path: str, rows: list[dict[str, str]]) -> str:
    if rows and rows[0].get("run_label"):
        return rows[0]["run_label"]
    return Path(path).stem


def main() -> None:
    args = parse_args()
    rows = read_predictions(args.predictions)
    label = args.label or infer_label(args.predictions, rows)
    concept_rows, dataset_rows = build_reports(rows)
    write_tsv(
        args.out_tsv,
        concept_rows,
        [
            "dataset",
            "concept",
            "forms",
            "gold_clusters",
            "pred_clusters",
            "pair_tp",
            "pair_fp",
            "pair_fn",
            "false_split_rate",
            "false_merge_rate",
        ],
    )
    write_tsv(
        args.out_dataset_tsv,
        dataset_rows,
        [
            "dataset",
            "concepts",
            "forms",
            "pair_tp",
            "pair_fp",
            "pair_fn",
            "false_split_rate_mean",
            "false_merge_rate_mean",
            "false_split_rate_global",
            "false_merge_rate_global",
        ],
    )
    if args.out_md:
        write_md(args.out_md, label, dataset_rows)


if __name__ == "__main__":
    main()
