#!/usr/bin/env python3
"""Run Cognator on an exported benchmark evaluation table.

This is the C/Python replacement for the old Go `cmd/bench` eval-table path:
it reads Track A/B tables from `scripts/export_arcaverborum_eval.py`, drives the
Python wrapper over the C core, and writes dataset-level benchmark metrics.
"""

from __future__ import annotations

import argparse
import csv
import time
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import cognator


@dataclass(frozen=True)
class EvalRow:
    eval_id: str
    dataset: str
    language_id: str
    concept: str
    segments: str
    gold_cognacy: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eval-table", required=True)
    parser.add_argument("--split-file", default="")
    parser.add_argument("--datasets", default="")
    parser.add_argument("--out-tsv", default="benchmark/cognator_results.tsv")
    parser.add_argument("--out-md", default="")
    parser.add_argument("--pred-out", default="")
    parser.add_argument("--run-label", default="")
    parser.add_argument("--preset", default="support-adaptive-bridge")
    parser.add_argument("--threshold", type=float, default=0.0, help="Positive values force a fixed graph threshold; 0 uses the preset adaptive policy.")
    parser.add_argument("--threshold-mode", default="", help="family or concept-shrink; blank leaves the preset default.")
    parser.add_argument("--threshold-min", type=float, default=None)
    parser.add_argument("--threshold-max", type=float, default=None)
    parser.add_argument("--threshold-shrink", type=float, default=None)
    parser.add_argument("--bootstrap", type=int, default=0)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--scorer", default="posterior")
    parser.add_argument("--min-pairs", type=int, default=1)
    parser.add_argument("--min-cognacy", type=int, default=50)
    parser.add_argument("--max-lects", type=int, default=250)
    parser.add_argument("--max-forms", type=int, default=15000)
    parser.add_argument("--max-pairs", type=int, default=3_000_000, help="Skip datasets with more same-concept cross-lect pairs; 0 disables the guard.")
    parser.add_argument("--track", default="A", choices=["A", "B"], help="Accepted for compatibility; metrics are identical for both tracks.")
    parser.add_argument("--merkmal-dir", default="", help="Accepted for compatibility; the C/Python package uses linked merkmal data.")
    parser.add_argument("--parallel-families", default="", help="Accepted for compatibility; this runner is sequential.")
    parser.add_argument("--jobs", default="", help="Accepted for compatibility; C jobs support is controlled by the C core options when exposed.")
    return parser.parse_args()


def load_name_list(path: str) -> set[str]:
    allowed: set[str] = set()
    if not path:
        return allowed
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            allowed.add(line)
    return allowed


def load_eval_rows(path: str, allowed: set[str]) -> list[EvalRow]:
    rows: list[EvalRow] = []
    with Path(path).open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        required = {"Eval_ID", "Dataset", "Language_ID", "Concept", "Segments", "Gold_Cognacy"}
        missing = required - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"missing required eval-table columns: {sorted(missing)}")
        for raw in reader:
            dataset = raw["Dataset"].strip()
            if allowed and dataset not in allowed:
                continue
            rows.append(
                EvalRow(
                    eval_id=raw["Eval_ID"].strip(),
                    dataset=dataset,
                    language_id=raw["Language_ID"].strip(),
                    concept=raw["Concept"].strip(),
                    segments=raw["Segments"].strip(),
                    gold_cognacy=raw["Gold_Cognacy"].strip(),
                )
            )
    return rows


def cognator_records(rows: list[EvalRow]) -> list[dict[str, str]]:
    return [
        {
            "ID": row.eval_id,
            "Language_ID": row.language_id,
            "Concept": row.concept,
            "Segments": row.segments,
            "Family": row.dataset,
            "Cognacy": row.gold_cognacy,
        }
        for row in rows
    ]


def estimate_pairs(rows: list[EvalRow]) -> int:
    by_concept: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    total = 0
    for row in rows:
        counts = by_concept[row.concept]
        total += sum(count for language, count in counts.items() if language != row.language_id)
        counts[row.language_id] += 1
    return total


def skipped_row(dataset: str, rows: list[EvalRow], threshold: float, reason: str) -> dict[str, object]:
    return {
        "dataset": dataset,
        "lects": len({row.language_id for row in rows}),
        "concepts": len({row.concept for row in rows}),
        "forms": len(rows),
        "cognacy_forms": sum(1 for row in rows if row.gold_cognacy),
        "estimated_pairs": estimate_pairs(rows),
        "oov_types": 0,
        "threshold": threshold,
        "clusters": 0,
        "precision": 0.0,
        "recall": 0.0,
        "f1": 0.0,
        "elapsed_ms": 0,
        "skipped": reason,
    }


def run_dataset(dataset: str, rows: list[EvalRow], args: argparse.Namespace) -> tuple[dict[str, object], list[dict[str, object]]]:
    lects = len({row.language_id for row in rows})
    concepts = len({row.concept for row in rows})
    cognacy_forms = sum(1 for row in rows if row.gold_cognacy)
    estimated_pairs = estimate_pairs(rows)
    if not rows:
        return skipped_row(dataset, rows, args.threshold, "no data"), []
    if cognacy_forms < args.min_cognacy:
        return skipped_row(dataset, rows, args.threshold, f"only {cognacy_forms} Cognacy-annotated forms (<{args.min_cognacy})"), []
    if lects < 2:
        return skipped_row(dataset, rows, args.threshold, "fewer than 2 lects"), []
    if args.max_lects > 0 and lects > args.max_lects:
        return skipped_row(dataset, rows, args.threshold, f"{lects} lects exceeds max-lects={args.max_lects}"), []
    if args.max_forms > 0 and len(rows) > args.max_forms:
        return skipped_row(dataset, rows, args.threshold, f"{len(rows)} forms exceeds max-forms={args.max_forms}"), []
    if args.max_pairs > 0 and estimated_pairs > args.max_pairs:
        return skipped_row(dataset, rows, args.threshold, f"{estimated_pairs} pairs exceeds max-pairs={args.max_pairs}"), []

    records = cognator_records(rows)
    by_id = {row.eval_id: row for row in rows}
    start = time.perf_counter()
    result = cognator.detect(
        records,
        preset=args.preset,
        threshold=args.threshold,
        threshold_mode=args.threshold_mode or None,
        threshold_min=args.threshold_min,
        threshold_max=args.threshold_max,
        threshold_shrink=args.threshold_shrink,
        bootstrap=args.bootstrap,
        scorer=args.scorer,
        min_pairs=args.min_pairs,
        seed=args.seed,
    )
    elapsed_ms = int((time.perf_counter() - start) * 1000)
    metrics = result.evaluate(records)
    resolved_threshold = result.resolved_threshold if result.resolved_threshold is not None else args.threshold
    predictions = []
    for pred_row in result:
        source = by_id[pred_row["ID"]]
        predictions.append(
            {
                "eval_id": pred_row["ID"],
                "dataset": dataset,
                "concept": source.concept,
                "gold": f"{source.concept}:{source.gold_cognacy}",
                "pred": f"{source.concept}:{pred_row['Cognator_ID']}",
                "confidence": pred_row.get("Cognator_Confidence", ""),
                "assigned_support": pred_row.get("Cognator_Assigned_Support", ""),
                "alternative_support": pred_row.get("Cognator_Alternative_Support", ""),
                "support_margin": pred_row.get("Cognator_Support_Margin", ""),
                "bootstrap": pred_row.get("Cognator_Bootstrap", ""),
                "resolved_threshold": resolved_threshold,
                "run_label": args.run_label,
            }
        )
    return (
        {
            "dataset": dataset,
            "lects": lects,
            "concepts": concepts,
            "forms": len(rows),
            "cognacy_forms": cognacy_forms,
            "estimated_pairs": estimated_pairs,
            "oov_types": 0,
            "threshold": resolved_threshold,
            "clusters": len(result.clusters()),
            "precision": metrics.precision,
            "recall": metrics.recall,
            "f1": metrics.f1,
            "elapsed_ms": elapsed_ms,
            "skipped": "",
        },
        predictions,
    )


def write_results(path: str, rows: list[dict[str, object]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fields = ["dataset", "lects", "concepts", "forms", "cognacy_forms", "estimated_pairs", "oov_types", "threshold", "clusters", "precision", "recall", "f1", "elapsed_ms", "skipped"]
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            out = dict(row)
            out["threshold"] = f"{float(out['threshold']):.4f}"
            for field in ("precision", "recall", "f1"):
                out[field] = f"{float(out[field]):.4f}"
            writer.writerow(out)


def write_markdown(path: str, rows: list[dict[str, object]]) -> None:
    if not path:
        return
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Cognator Benchmark", "", "| Dataset | Forms | Precision | Recall | F1 | Skipped |", "|---|---:|---:|---:|---:|---|"]
    for row in rows:
        lines.append(
            f"| {row['dataset']} | {row['forms']} | {float(row['precision']):.4f} | "
            f"{float(row['recall']):.4f} | {float(row['f1']):.4f} | {row['skipped']} |"
        )
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_predictions(path: str, rows: list[dict[str, object]]) -> None:
    if not path:
        return
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fields = ["eval_id", "dataset", "concept", "gold", "pred", "confidence", "assigned_support", "alternative_support", "support_margin", "bootstrap", "resolved_threshold", "run_label"]
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def main() -> None:
    args = parse_args()
    allowed = set(item.strip() for item in args.datasets.split(",") if item.strip())
    split_names = load_name_list(args.split_file)
    if split_names:
        allowed = split_names if not allowed else allowed & split_names
    rows = load_eval_rows(args.eval_table, allowed)
    grouped: dict[str, list[EvalRow]] = defaultdict(list)
    for row in rows:
        grouped[row.dataset].append(row)
    results = []
    predictions = []
    for dataset in sorted(grouped):
        result, pred = run_dataset(dataset, grouped[dataset], args)
        results.append(result)
        predictions.extend(pred)
    write_results(args.out_tsv, results)
    write_markdown(args.out_md, results)
    write_predictions(args.pred_out, predictions)


if __name__ == "__main__":
    main()
