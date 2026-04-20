#!/usr/bin/env python3
"""Run a LingPy baseline on an exported benchmark evaluation table.

Default target:
- LexStat scorer
- threshold = 0.55
- cluster_method = infomap

The script writes dataset-level metrics in the same TSV layout used by
`benchmark/lingpy_results.tsv` and can optionally emit per-form
predictions for downstream comparison and error analysis.
"""

from __future__ import annotations

import argparse
import csv
import io
import time
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory

from lingpy import LexStat


@dataclass
class EvalRow:
    eval_id: str
    dataset: str
    language_id: str
    concept: str
    segments: str
    gold_cognacy: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--eval-table",
        required=True,
        help="Track A or Track B TSV exported by scripts/export_arcaverborum_eval.py",
    )
    parser.add_argument(
        "--split-file",
        default="",
        help="Optional file with one dataset per line",
    )
    parser.add_argument(
        "--datasets",
        default="",
        help="Optional comma-separated dataset filter",
    )
    parser.add_argument(
        "--out-tsv",
        default="benchmark/lingpy_results.tsv",
        help="Dataset-level TSV output",
    )
    parser.add_argument(
        "--pred-out",
        default="",
        help="Optional per-form prediction TSV output",
    )
    parser.add_argument(
        "--method",
        default="lexstat",
        choices=["lexstat", "sca"],
        help="LingPy clustering method",
    )
    parser.add_argument(
        "--cluster-method",
        default="infomap",
        choices=["infomap", "upgma", "mcl", "single", "complete"],
        help="LingPy cluster method",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.55,
        help="Clustering threshold",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=1000,
        help="LexStat scorer permutation runs",
    )
    parser.add_argument(
        "--min-cognacy",
        type=int,
        default=50,
        help="Skip datasets with fewer than N cognacy-annotated rows",
    )
    parser.add_argument(
        "--max-lects",
        type=int,
        default=250,
        help="Skip datasets with more than N lects; 0 disables guard",
    )
    parser.add_argument(
        "--max-forms",
        type=int,
        default=15000,
        help="Skip datasets with more than N forms; 0 disables guard",
    )
    parser.add_argument(
        "--run-label",
        default="",
        help="Optional run label for prediction output",
    )
    return parser.parse_args()


def load_name_list(path: str) -> set[str]:
    allowed: set[str] = set()
    if not path:
        return allowed
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        allowed.add(line)
    return allowed


def load_eval_rows(path: str, allowed: set[str]) -> list[EvalRow]:
    rows: list[EvalRow] = []
    with Path(path).open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        required = {
            "Eval_ID",
            "Dataset",
            "Language_ID",
            "Concept",
            "Segments",
            "Gold_Cognacy",
        }
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


def bcubed(pred: dict[str, str], truth: dict[str, str]) -> tuple[float, float, float]:
    pred_members = defaultdict(list)
    truth_members = defaultdict(list)
    for key, value in pred.items():
        pred_members[value].append(key)
    for key, value in truth.items():
        truth_members[value].append(key)

    p_sum = 0.0
    r_sum = 0.0
    n = 0
    for eval_id, pred_cluster in pred.items():
        gold_cluster = truth.get(eval_id)
        if not gold_cluster:
            continue
        pred_set = pred_members[pred_cluster]
        truth_set = truth_members[gold_cluster]
        truth_lookup = set(truth_set)
        shared = sum(1 for item in pred_set if item in truth_lookup)
        p_sum += shared / max(1, len(pred_set))
        r_sum += shared / max(1, len(truth_set))
        n += 1
    if n == 0:
        return 0.0, 0.0, 0.0
    precision = p_sum / n
    recall = r_sum / n
    if precision + recall == 0:
        return precision, recall, 0.0
    return precision, recall, 2 * precision * recall / (precision + recall)


def ensure_dir(path: str) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)


def lingpy_input(rows: list[EvalRow]) -> str:
    buf = io.StringIO()
    writer = csv.writer(buf, delimiter="\t", lineterminator="\n")
    writer.writerow(["ID", "doculect", "concept", "tokens"])
    for idx, row in enumerate(rows, start=1):
        writer.writerow([idx, row.language_id, row.concept, row.segments])
    return buf.getvalue()


def has_igraph() -> bool:
    try:
        import igraph  # noqa: F401
    except Exception:
        return False
    return True


def run_dataset(
    dataset: str,
    rows: list[EvalRow],
    *,
    method: str,
    cluster_method: str,
    threshold: float,
    runs: int,
    min_cognacy: int,
    max_lects: int,
    max_forms: int,
) -> tuple[dict[str, object], list[dict[str, object]]]:
    lects = len({row.language_id for row in rows})
    concepts = len({row.concept for row in rows})
    forms = len(rows)
    cognacy_forms = sum(1 for row in rows if row.gold_cognacy)
    result: dict[str, object] = {
        "dataset": dataset,
        "lects": lects,
        "concepts": concepts,
        "forms": forms,
        "cognacy_forms": cognacy_forms,
        "oov_types": 0,
        "threshold": threshold,
        "clusters": 0,
        "precision": 0.0,
        "recall": 0.0,
        "f1": 0.0,
        "elapsed_ms": 0,
        "skipped": "",
    }
    predictions: list[dict[str, object]] = []

    if forms == 0:
        result["skipped"] = "no data"
        return result, predictions
    if cognacy_forms < min_cognacy:
        result["skipped"] = f"only {cognacy_forms} Cognacy-annotated forms (<{min_cognacy})"
        return result, predictions
    if lects < 2:
        result["skipped"] = "fewer than 2 lects"
        return result, predictions
    if max_lects > 0 and lects > max_lects:
        result["skipped"] = f"{lects} lects exceeds max-lects={max_lects}"
        return result, predictions
    if max_forms > 0 and forms > max_forms:
        result["skipped"] = f"{forms} forms exceeds max-forms={max_forms}"
        return result, predictions
    if cluster_method == "infomap" and not has_igraph():
        result["skipped"] = "python-igraph is required for LingPy infomap clustering"
        return result, predictions

    start = time.perf_counter()
    with TemporaryDirectory() as tmpdir:
        input_path = Path(tmpdir) / f"{dataset}.tsv"
        input_path.write_text(lingpy_input(rows), encoding="utf-8")
        lex = LexStat(str(input_path))
        if method == "lexstat":
            lex.get_scorer(runs=runs)
        ref = "lingpyid"
        lex.cluster(method=method, threshold=threshold, cluster_method=cluster_method, ref=ref)
        pred = {}
        truth = {}
        cluster_ids = set()
        for idx, row in enumerate(rows, start=1):
            cluster_value = str(lex[idx, ref])
            pred_label = f"{row.concept}:{cluster_value}"
            gold_label = f"{row.concept}:{row.gold_cognacy}"
            pred[row.eval_id] = pred_label
            truth[row.eval_id] = gold_label
            cluster_ids.add(pred_label)
            predictions.append(
                {
                    "eval_id": row.eval_id,
                    "dataset": dataset,
                    "concept": row.concept,
                    "gold": gold_label,
                    "pred": pred_label,
                    "confidence": "",
                    "bootstrap": "",
                }
            )
        precision, recall, f1 = bcubed(pred, truth)
        elapsed_ms = int((time.perf_counter() - start) * 1000)
        result.update(
            {
                "clusters": len(cluster_ids),
                "precision": precision,
                "recall": recall,
                "f1": f1,
                "elapsed_ms": elapsed_ms,
            }
        )
    return result, predictions


def write_results(path: str, rows: list[dict[str, object]]) -> None:
    ensure_dir(path)
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            [
                "dataset",
                "lects",
                "concepts",
                "forms",
                "cognacy_forms",
                "oov_types",
                "threshold",
                "clusters",
                "precision",
                "recall",
                "f1",
                "elapsed_ms",
                "skipped",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row["dataset"],
                    row["lects"],
                    row["concepts"],
                    row["forms"],
                    row["cognacy_forms"],
                    row["oov_types"],
                    f"{float(row['threshold']):.4f}",
                    row["clusters"],
                    f"{float(row['precision']):.4f}",
                    f"{float(row['recall']):.4f}",
                    f"{float(row['f1']):.4f}",
                    row["elapsed_ms"],
                    row["skipped"],
                ]
            )


def write_predictions(path: str, rows: list[dict[str, object]], run_label: str) -> None:
    ensure_dir(path)
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(["eval_id", "dataset", "concept", "gold", "pred", "confidence", "bootstrap", "run_label"])
        for row in rows:
            writer.writerow(
                [
                    row["eval_id"],
                    row["dataset"],
                    row["concept"],
                    row["gold"],
                    row["pred"],
                    row["confidence"],
                    row["bootstrap"],
                    run_label,
                ]
            )


def main() -> None:
    args = parse_args()
    allowed = set()
    if args.datasets:
        allowed.update(item.strip() for item in args.datasets.split(",") if item.strip())
    if args.split_file:
        split_names = load_name_list(args.split_file)
        allowed = split_names if not allowed else allowed & split_names

    rows = load_eval_rows(args.eval_table, allowed)
    grouped: dict[str, list[EvalRow]] = defaultdict(list)
    for row in rows:
        grouped[row.dataset].append(row)

    results: list[dict[str, object]] = []
    predictions: list[dict[str, object]] = []
    for dataset in sorted(grouped):
        result, pred = run_dataset(
            dataset,
            grouped[dataset],
            method=args.method,
            cluster_method=args.cluster_method,
            threshold=args.threshold,
            runs=args.runs,
            min_cognacy=args.min_cognacy,
            max_lects=args.max_lects,
            max_forms=args.max_forms,
        )
        results.append(result)
        predictions.extend(pred)

    write_results(args.out_tsv, results)
    if args.pred_out:
        write_predictions(args.pred_out, predictions, args.run_label)


if __name__ == "__main__":
    main()
