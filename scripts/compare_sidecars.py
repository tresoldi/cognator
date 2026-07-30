#!/usr/bin/env python3
import argparse
import csv
import itertools
import math
import sys


def dialect_for(path):
    return "\t" if path.endswith(".tsv") else ","


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter=dialect_for(path)))


def read_sidecar(path):
    rows = read_rows(path)
    missing = {"ID", "Cognator_ID"} - set(rows[0].keys() if rows else [])
    if missing:
        raise ValueError(f"{path}: missing columns: {', '.join(sorted(missing))}")
    return {row["ID"]: row["Cognator_ID"] for row in rows}


def read_truth(path):
    rows = read_rows(path)
    missing = {"ID", "Concept", "Cognacy"} - set(rows[0].keys() if rows else [])
    if missing:
        raise ValueError(f"{path}: missing columns: {', '.join(sorted(missing))}")
    return {
        row["ID"]: {
            "concept": row["Concept"],
            "truth": f"{row['Concept']}\x1f{row['Cognacy']}",
        }
        for row in rows
    }


def b_cubed(pred, truth, ids):
    precision_sum = 0.0
    recall_sum = 0.0
    for item in ids:
        pred_cluster = {candidate for candidate in ids if pred[candidate] == pred[item]}
        truth_cluster = {candidate for candidate in ids if truth[candidate]["truth"] == truth[item]["truth"]}
        overlap = len(pred_cluster & truth_cluster)
        precision_sum += overlap / len(pred_cluster)
        recall_sum += overlap / len(truth_cluster)
    if not ids:
        return (0.0, 0.0, 0.0)
    precision = precision_sum / len(ids)
    recall = recall_sum / len(ids)
    f1 = 0.0 if precision + recall == 0 else 2.0 * precision * recall / (precision + recall)
    return (precision, recall, f1)


def pair_agreement(left, right, truth, ids):
    total = 0
    agree = 0
    for a, b in itertools.combinations(ids, 2):
        if truth[a]["concept"] != truth[b]["concept"]:
            continue
        total += 1
        if (left[a] == left[b]) == (right[a] == right[b]):
            agree += 1
    return (agree, total, 1.0 if total == 0 else agree / total)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", required=True)
    parser.add_argument("--right", required=True)
    parser.add_argument("--truth", required=True)
    parser.add_argument("--left-label", default="left")
    parser.add_argument("--right-label", default="right")
    parser.add_argument("--min-pair-agreement", type=float, default=1.0)
    parser.add_argument("--max-f1-delta", type=float, default=0.0)
    args = parser.parse_args()

    try:
        left = read_sidecar(args.left)
        right = read_sidecar(args.right)
        truth = read_truth(args.truth)
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 2

    ids = sorted(set(left) & set(right) & set(truth))
    if not ids:
        print("no shared scored IDs", file=sys.stderr)
        return 2

    left_scores = b_cubed(left, truth, ids)
    right_scores = b_cubed(right, truth, ids)
    agree, total, ratio = pair_agreement(left, right, truth, ids)
    f1_delta = abs(left_scores[2] - right_scores[2])

    print(f"items\t{len(ids)}")
    print(f"pairs_agree\t{agree}")
    print(f"pairs_total\t{total}")
    print(f"pair_agreement\t{ratio:.6f}")
    print(
        f"{args.left_label}\tprecision={left_scores[0]:.6f}\t"
        f"recall={left_scores[1]:.6f}\tf1={left_scores[2]:.6f}"
    )
    print(
        f"{args.right_label}\tprecision={right_scores[0]:.6f}\t"
        f"recall={right_scores[1]:.6f}\tf1={right_scores[2]:.6f}"
    )
    print(f"f1_delta\t{f1_delta:.6f}")

    if ratio + 1e-12 < args.min_pair_agreement:
        print(
            f"pair agreement {ratio:.6f} is below {args.min_pair_agreement:.6f}",
            file=sys.stderr,
        )
        return 1
    if f1_delta - args.max_f1_delta > 1e-12 and not math.isclose(f1_delta, args.max_f1_delta):
        print(f"F1 delta {f1_delta:.6f} exceeds {args.max_f1_delta:.6f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
