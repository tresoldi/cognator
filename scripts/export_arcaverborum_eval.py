#!/usr/bin/env python3
"""Export benchmark-ready evaluation tables from an arcaverborum forms table.

The exporter creates two tracks:

- Track A: simple full-form cognacy
- Track B: partial/complex cognacy

Track A is the default optimization target. It excludes rows with
semicolon-separated cognacy and rows carrying partial-cognacy
annotations.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--forms",
        help="Path to arcaverborum forms.csv or forms.tsv",
    )
    parser.add_argument(
        "--arcaverborum-root",
        default="",
        help="Path to the arcaverborum repository root; used to auto-locate a built collection",
    )
    parser.add_argument(
        "--collection",
        default="aggregate",
        choices=("aggregate", "full", "core", "corecog"),
        help="arcaverborum output/<collection>/forms.csv (default: aggregate, the bestof-rewrite layout)",
    )
    parser.add_argument(
        "--datasets-csv",
        default="../arcaverborum/datasets.csv",
        help="Path to arcaverborum datasets.csv",
    )
    parser.add_argument(
        "--out-dir",
        default="benchmark/export",
        help="Directory for exported benchmark tables",
    )
    parser.add_argument(
        "--dataset-list",
        default="",
        help="Optional comma-separated dataset subset; empty = all CoreCog datasets",
    )
    args = parser.parse_args()
    if not args.forms and not args.arcaverborum_root:
        parser.error("one of --forms or --arcaverborum-root is required")
    return args


def delimiter_for(path: Path) -> str:
    return "," if path.suffix.lower() == ".csv" else "\t"


def resolve_forms_path(args: argparse.Namespace) -> Path:
    if args.forms:
        return Path(args.forms)
    root = Path(args.arcaverborum_root)
    candidate = root / "output" / args.collection / "forms.csv"
    if candidate.exists():
        return candidate
    raise FileNotFoundError(
        f"could not find built collection at {candidate}; run "
        f"'python {root / 'build.py'} aggregate' first or pass --forms explicitly"
    )


def load_corecog_datasets(path: Path) -> list[str]:
    # bestof-rewrite renamed the marker column CoreCog -> ExpertCognates
    # (same 58-dataset subset). Accept either so old datasets.csv dumps
    # keep working.
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        return sorted(
            row["NAME"] for row in reader
            if (row.get("CoreCog") or row.get("ExpertCognates") or "").strip().upper() == "TRUE"
        )


def requested_datasets(corecog: list[str], dataset_list: str) -> list[str]:
    if not dataset_list.strip():
        return corecog
    allowed = {item.strip() for item in dataset_list.split(",") if item.strip()}
    return [name for name in corecog if name in allowed]


def nonempty(value: str | None) -> bool:
    return value is not None and value.strip() != ""


def clean(value: str | None) -> str:
    return value.strip() if value is not None else ""


def concept_of(row: dict[str, str]) -> str:
    # CoreCog-export aliases first, then bestof-rewrite native names.
    for key in ("Concept", "Concepticon_Gloss", "Parameter_ID", "concept_id", "concept_label"):
        if nonempty(row.get(key)):
            return clean(row[key])
    return ""


def dataset_of(row: dict[str, str]) -> str:
    # Native forms.csv has no Dataset column; transcription_source carries it.
    for key in ("Dataset", "transcription_source"):
        v = clean(row.get(key))
        if v:
            return v
    return ""


def language_of(row: dict[str, str]) -> str:
    for key in ("Language_ID", "av_id"):
        v = clean(row.get(key))
        if v:
            return v
    return ""


def raw_id_of(row: dict[str, str], row_num: int) -> str:
    # The bestof-rewrite aggregate has no per-form ID; synthesize a key
    # reconstructable from forms.csv (av_id + source_form_id), with the
    # row number as a fallback. Mirrors cognator's Go reader.
    rid = clean(row.get("ID"))
    if rid:
        return rid
    lang = language_of(row)
    if not lang:
        return ""
    fid = clean(row.get("source_form_id"))
    if fid:
        return f"{lang}::{fid}"
    return f"{lang}::r{row_num}"


def has_partial_cognacy(row: dict[str, str]) -> bool:
    return nonempty(row.get("Morpheme_Index")) or nonempty(row.get("Segment_Slice"))


def has_multi_cognacy(row: dict[str, str]) -> bool:
    return ";" in clean(row.get("Cognacy"))


def track_of(row: dict[str, str]) -> str | None:
    if not nonempty(row.get("Segments")) or not nonempty(row.get("Cognacy")):
        return None
    if has_partial_cognacy(row) or has_multi_cognacy(row):
        return "B"
    return "A"


def export_rows(
    forms_path: Path,
    datasets: set[str],
) -> tuple[list[dict[str, str]], list[dict[str, str]], dict[str, dict[str, int]]]:
    track_a: list[dict[str, str]] = []
    track_b: list[dict[str, str]] = []
    raw_id_counts: dict[str, Counter[str]] = defaultdict(Counter)
    stats: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))

    with forms_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter=delimiter_for(forms_path))
        if reader.fieldnames is None:
            raise ValueError(f"{forms_path} has no header row")

        for row_num, row in enumerate(reader, start=2):
            dataset = dataset_of(row)
            if dataset not in datasets:
                continue

            stats[dataset]["rows_total"] += 1

            raw_id = raw_id_of(row, row_num)
            if not nonempty(row.get("Segments")):
                stats[dataset]["rows_missing_segments"] += 1
                continue
            if not nonempty(row.get("Cognacy")):
                stats[dataset]["rows_missing_cognacy"] += 1
                continue

            track = track_of(row)
            if track is None:
                continue

            if has_partial_cognacy(row):
                stats[dataset]["rows_partial"] += 1
            if has_multi_cognacy(row):
                stats[dataset]["rows_multi_cognacy"] += 1

            raw_id_counts[dataset][raw_id] += 1
            occurrence = raw_id_counts[dataset][raw_id]
            eval_id = f"{dataset}:{raw_id}#{occurrence}"

            out_row = {
                "Eval_ID": eval_id,
                "Raw_ID": raw_id,
                "Dataset": dataset,
                "Language_ID": language_of(row),
                "Concept": concept_of(row),
                "Segments": clean(row.get("Segments")),
                "Gold_Cognacy": clean(row.get("Cognacy")),
                "Track": track,
                "Has_Partial": "1" if has_partial_cognacy(row) else "0",
                "Has_Multi_Cognacy": "1" if has_multi_cognacy(row) else "0",
                "Row_Number": str(row_num),
            }

            if occurrence > 1:
                stats[dataset]["rows_duplicate_raw_id"] += 1

            if track == "A":
                track_a.append(out_row)
                stats[dataset]["track_a_rows"] += 1
            else:
                track_b.append(out_row)
                stats[dataset]["track_b_rows"] += 1

    return track_a, track_b, stats


def write_tsv(path: Path, rows: Iterable[dict[str, str]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_dataset_summary(path: Path, stats: dict[str, dict[str, int]]) -> None:
    fieldnames = [
        "dataset",
        "rows_total",
        "rows_missing_segments",
        "rows_missing_cognacy",
        "rows_partial",
        "rows_multi_cognacy",
        "rows_duplicate_raw_id",
        "track_a_rows",
        "track_b_rows",
    ]
    rows = []
    for dataset in sorted(stats):
        row = {"dataset": dataset}
        for key in fieldnames[1:]:
            row[key] = stats[dataset].get(key, 0)
        rows.append(row)
    write_tsv(path, rows, fieldnames)


def write_manifest(
    path: Path,
    *,
    forms_path: Path,
    datasets_csv: Path,
    selected_datasets: list[str],
    track_a_path: Path,
    track_b_path: Path,
    summary_path: Path,
) -> None:
    payload = {
        "source_forms": str(forms_path),
        "source_datasets_csv": str(datasets_csv),
        "selected_corecog_datasets": selected_datasets,
        "tracks": {
            "A": {
                "path": str(track_a_path),
                "description": "Simple full-form cognacy; excludes partial and semicolon cognacy rows.",
            },
            "B": {
                "path": str(track_b_path),
                "description": "Partial/complex cognacy rows, including semicolon and partial annotations.",
            },
        },
        "dataset_summary": str(summary_path),
        "export_policy": {
            "exclude_missing_segments": True,
            "exclude_missing_cognacy": True,
            "track_a_excludes_semicolon_cognacy": True,
            "track_a_excludes_partial_cognacy": True,
            "eval_id_format": "dataset:raw_id#occurrence",
        },
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    forms_path = resolve_forms_path(args)
    datasets_csv = Path(args.datasets_csv)
    out_dir = Path(args.out_dir)

    corecog = load_corecog_datasets(datasets_csv)
    selected = requested_datasets(corecog, args.dataset_list)
    selected_set = set(selected)

    track_a, track_b, stats = export_rows(forms_path, selected_set)

    fieldnames = [
        "Eval_ID",
        "Raw_ID",
        "Dataset",
        "Language_ID",
        "Concept",
        "Segments",
        "Gold_Cognacy",
        "Track",
        "Has_Partial",
        "Has_Multi_Cognacy",
        "Row_Number",
    ]
    track_a_path = out_dir / "track_a.tsv"
    track_b_path = out_dir / "track_b.tsv"
    summary_path = out_dir / "dataset_summary.tsv"
    manifest_path = out_dir / "manifest.json"

    write_tsv(track_a_path, track_a, fieldnames)
    write_tsv(track_b_path, track_b, fieldnames)
    write_dataset_summary(summary_path, stats)
    write_manifest(
        manifest_path,
        forms_path=forms_path,
        datasets_csv=datasets_csv,
        selected_datasets=selected,
        track_a_path=track_a_path,
        track_b_path=track_b_path,
        summary_path=summary_path,
    )


if __name__ == "__main__":
    main()
