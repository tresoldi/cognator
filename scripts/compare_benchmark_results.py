#!/usr/bin/env python3
"""Join Cognator and LingPy benchmark result TSVs.

This replaces the old Go `cmd/benchcompare` tool for benchmark reports.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cognator", required=True)
    parser.add_argument("--lingpy", required=True)
    parser.add_argument("--out-tsv", required=True)
    parser.add_argument("--out-md", default="")
    return parser.parse_args()


def read_results(path: str) -> dict[str, dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        return {row["dataset"]: row for row in csv.DictReader(handle, delimiter="\t")}


def number(row: dict[str, str] | None, field: str) -> float:
    if not row:
        return 0.0
    try:
        return float(row.get(field, "") or 0.0)
    except ValueError:
        return 0.0


def integer(row: dict[str, str] | None, field: str) -> int:
    if not row:
        return 0
    try:
        return int(float(row.get(field, "") or 0))
    except ValueError:
        return 0


def compare_rows(cognator: dict[str, dict[str, str]], lingpy: dict[str, dict[str, str]]) -> list[dict[str, object]]:
    rows = []
    for dataset in sorted(set(cognator) | set(lingpy)):
        c_row = cognator.get(dataset)
        l_row = lingpy.get(dataset)
        c_f = number(c_row, "f1")
        l_f = number(l_row, "f1")
        rows.append(
            {
                "dataset": dataset,
                "lects": integer(c_row or l_row, "lects"),
                "forms": integer(c_row or l_row, "forms"),
                "cognator_P": number(c_row, "precision"),
                "cognator_R": number(c_row, "recall"),
                "cognator_F": c_f,
                "cognator_ms": integer(c_row, "elapsed_ms"),
                "lingpy_P": number(l_row, "precision"),
                "lingpy_R": number(l_row, "recall"),
                "lingpy_F": l_f,
                "lingpy_ms": integer(l_row, "elapsed_ms"),
                "delta_F": c_f - l_f,
                "cognator_skipped": (c_row or {}).get("skipped", ""),
                "lingpy_skipped": (l_row or {}).get("skipped", ""),
            }
        )
    return rows


def write_tsv(path: str, rows: list[dict[str, object]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fields = ["dataset", "lects", "forms", "cognator_P", "cognator_R", "cognator_F", "cognator_ms", "lingpy_P", "lingpy_R", "lingpy_F", "lingpy_ms", "delta_F", "cognator_skipped", "lingpy_skipped"]
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            out = dict(row)
            for field in ("cognator_P", "cognator_R", "cognator_F", "lingpy_P", "lingpy_R", "lingpy_F"):
                out[field] = f"{float(out[field]):.4f}"
            out["delta_F"] = f"{float(out['delta_F']):+.4f}"
            writer.writerow(out)


def write_md(path: str, rows: list[dict[str, object]]) -> None:
    if not path:
        return
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Cognator vs LingPy", "", "| Dataset | Cognator F1 | LingPy F1 | Delta | Forms |", "|---|---:|---:|---:|---:|"]
    for row in rows:
        lines.append(f"| {row['dataset']} | {float(row['cognator_F']):.4f} | {float(row['lingpy_F']):.4f} | {float(row['delta_F']):+.4f} | {row['forms']} |")
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    rows = compare_rows(read_results(args.cognator), read_results(args.lingpy))
    write_tsv(args.out_tsv, rows)
    write_md(args.out_md, rows)


if __name__ == "__main__":
    main()
