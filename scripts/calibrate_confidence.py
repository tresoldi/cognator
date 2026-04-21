#!/usr/bin/env python3
"""Fit and apply a confidence calibration to prediction TSVs.

Two subcommands:

- `fit`:   reads a predictions TSV (typically a Dev-Core run), treats
           form-level pairwise error involvement as the "wrong" label,
           and fits a monotone (isotonic) calibration of raw confidence
           to P(correct | confidence). Writes a calibration JSON file
           plus a markdown reliability report. Optionally stratifies by
           the `cluster_size` column so stable singletons and
           unsupported singletons get separate calibration curves.

- `apply`: reads a calibration JSON and a predictions TSV, writes an
           enriched TSV with two new columns: `confidence_calibrated`
           (isotonic-mapped) and `risk_band` (low | medium | high).

Design choices:

- Ground label is binary error involvement — "correct" iff the form is
  not in any same-concept false-merge or false-split pair. This is the
  same notion used by scripts/confidence_report.py, so calibration
  inherits that report's definition of error.
- Calibration is pool-adjacent-violators (PAV) isotonic regression on
  equal-count bins of the fit rows. Stdlib only; no sklearn dependency.
- Stratification by `cluster_size` (singleton vs multi-member)
  addresses the conf=1.0 collapse flagged in docs/roadmap.md Model
  §2/§4: stable singletons and unsupported singletons with
  competitors both live at conf=1.0 under the single-metric view.
- Risk bands are fixed thresholds on the calibrated value (not on the
  raw confidence). Defaults: low ≥ 0.90, medium ≥ 0.60, else high.
- The calibration file is a portable plain-JSON artifact. Fit once on
  Dev-Core, apply to any run that emits the same `confidence` column.
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import defaultdict
from itertools import combinations
from pathlib import Path
from typing import Iterable


REQUIRED_COLUMNS = {"eval_id", "dataset", "concept", "gold", "pred", "confidence"}
STRATIFY_CHOICES = ("", "cluster_size")
DEFAULT_STRATUM = "_all"
SINGLETON_STRATUM = "singleton"
MULTI_STRATUM = "multi"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)

    fit = sub.add_parser("fit", help="Fit a calibration from a predictions TSV.")
    fit.add_argument("--predictions", required=True, help="Prediction TSV from cmd/bench --pred-out")
    fit.add_argument("--out-json", required=True, help="Calibration JSON output path")
    fit.add_argument("--out-md", default="", help="Optional reliability-report markdown path")
    fit.add_argument("--bins", type=int, default=20, help="Number of equal-count bins per stratum (default 20)")
    fit.add_argument("--band-low", type=float, default=0.90, help="Calibrated value at or above which risk_band is 'low' (default 0.90)")
    fit.add_argument("--band-high", type=float, default=0.60, help="Calibrated value below which risk_band is 'high' (default 0.60)")
    fit.add_argument(
        "--stratify-by",
        choices=STRATIFY_CHOICES,
        default="",
        help="Partition fit rows before PAV: '' (default, single fit) or 'cluster_size' (singleton vs multi-member cluster)",
    )

    apply_ = sub.add_parser("apply", help="Apply a calibration JSON to a predictions TSV.")
    apply_.add_argument("--calibration", required=True, help="Calibration JSON from 'fit'")
    apply_.add_argument("--predictions", required=True, help="Prediction TSV to enrich")
    apply_.add_argument("--out-tsv", required=True, help="Enriched TSV output path")
    apply_.add_argument("--out-md", default="", help="Optional transfer-evaluation markdown (requires gold labels in --predictions)")

    return parser.parse_args()


def read_predictions(path: str) -> list[dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        missing = REQUIRED_COLUMNS - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"{path}: missing required columns {sorted(missing)}")
        return list(reader)


def error_involved_ids(rows: list[dict[str, str]]) -> set[str]:
    ids: set[str] = set()
    by_concept: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row["gold"]:
            by_concept[(row["dataset"], row["concept"])].append(row)
    for members in by_concept.values():
        for left, right in combinations(members, 2):
            gold_same = left["gold"] == right["gold"]
            pred_same = left["pred"] == right["pred"]
            if gold_same != pred_same:
                ids.add(left["eval_id"])
                ids.add(right["eval_id"])
    return ids


def pool_adjacent_violators(ys: list[float], ws: list[float]) -> list[float]:
    """Pool-adjacent-violators: monotone non-decreasing weighted fit.

    ys is the per-bin target (e.g. empirical accuracy); ws is the bin
    count. Returns an isotonic sequence of length len(ys). Simple O(n²)
    merge loop — fine for bin counts in the low hundreds.
    """
    n = len(ys)
    if n == 0:
        return []
    vals = list(ys)
    sizes = [1] * n
    weights = list(ws)
    changed = True
    while changed:
        changed = False
        i = 0
        while i < len(vals) - 1:
            if vals[i] > vals[i + 1]:
                total_w = weights[i] + weights[i + 1]
                vals[i] = (vals[i] * weights[i] + vals[i + 1] * weights[i + 1]) / total_w
                sizes[i] += sizes[i + 1]
                weights[i] = total_w
                del vals[i + 1]
                del sizes[i + 1]
                del weights[i + 1]
                changed = True
            else:
                i += 1
    expanded: list[float] = []
    for v, s in zip(vals, sizes):
        expanded.extend([v] * s)
    return expanded


def stratum_for(row: dict[str, str], stratify_by: str) -> str:
    if stratify_by == "":
        return DEFAULT_STRATUM
    if stratify_by == "cluster_size":
        size = int(row.get("cluster_size", "1") or "1")
        return SINGLETON_STRATUM if size <= 1 else MULTI_STRATUM
    raise ValueError(f"unknown stratify_by {stratify_by!r}")


def fit_stratum(rows: list[dict[str, str]], error_ids: set[str], bins: int) -> dict:
    samples = sorted(
        [(float(row["confidence"]), 0.0 if row["eval_id"] in error_ids else 1.0) for row in rows],
        key=lambda t: t[0],
    )
    n = len(samples)
    if n == 0:
        return {"fit_rows": 0, "error_involved_rows": 0, "bins": 0, "mapping": [], "raw_bins": []}
    bins = max(2, min(bins, n))
    per_bin = max(1, n // bins)
    bin_centers: list[float] = []
    bin_means: list[float] = []
    bin_counts: list[int] = []
    idx = 0
    while idx < n:
        end = min(idx + per_bin, n)
        if n - end < per_bin and idx + per_bin < n:
            end = n
        chunk = samples[idx:end]
        confs = [c for c, _ in chunk]
        labels = [y for _, y in chunk]
        bin_centers.append(statistics.mean(confs))
        bin_means.append(statistics.mean(labels))
        bin_counts.append(len(chunk))
        idx = end
    isotonic = pool_adjacent_violators(bin_means, [float(c) for c in bin_counts])
    mapping: list[list[float]] = []
    for center, value in zip(bin_centers, isotonic):
        if mapping and abs(mapping[-1][1] - value) < 1e-12:
            mapping[-1] = [round(center, 6), round(value, 6)]
        else:
            mapping.append([round(center, 6), round(value, 6)])
    errors = sum(1 for row in rows if row["eval_id"] in error_ids)
    return {
        "fit_rows": n,
        "error_involved_rows": errors,
        "bins": len(bin_centers),
        "mapping": mapping,
        "raw_bins": [
            {"center": round(bc, 6), "mean": round(bm, 6), "calibrated": round(iv, 6), "count": bn}
            for bc, bm, iv, bn in zip(bin_centers, bin_means, isotonic, bin_counts)
        ],
    }


def fit_calibration(rows: list[dict[str, str]], bins: int, band_low: float, band_high: float, stratify_by: str) -> dict:
    gold_rows = [row for row in rows if row["gold"]]
    if not gold_rows:
        raise ValueError("no rows carry gold labels; cannot fit a calibration")
    if stratify_by == "cluster_size" and "cluster_size" not in (gold_rows[0].keys() if gold_rows else ()):
        raise ValueError("--stratify-by cluster_size requires a cluster_size column in the predictions TSV")
    error_ids = error_involved_ids(gold_rows)
    strata_rows: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in gold_rows:
        strata_rows[stratum_for(row, stratify_by)].append(row)
    strata = {name: fit_stratum(members, error_ids, bins) for name, members in strata_rows.items()}
    return {
        "method": "isotonic-pav",
        "stratify_by": stratify_by,
        "fit_rows": len(gold_rows),
        "error_involved_rows": len(error_ids),
        "bands": {"low": band_low, "high": band_high},
        "strata": strata,
    }


def interpolate(mapping: list[list[float]], x: float) -> float:
    if not mapping:
        return x
    if x <= mapping[0][0]:
        return mapping[0][1]
    if x >= mapping[-1][0]:
        return mapping[-1][1]
    for i in range(len(mapping) - 1):
        lo_x, lo_y = mapping[i]
        hi_x, hi_y = mapping[i + 1]
        if lo_x <= x <= hi_x:
            if hi_x == lo_x:
                return lo_y
            frac = (x - lo_x) / (hi_x - lo_x)
            return lo_y + frac * (hi_y - lo_y)
    return mapping[-1][1]


def risk_band(calibrated: float, low: float, high: float) -> str:
    if calibrated >= low:
        return "low"
    if calibrated < high:
        return "high"
    return "medium"


def lookup_mapping(calibration: dict, row: dict[str, str]) -> list[list[float]]:
    stratify_by = calibration.get("stratify_by", "")
    strata = calibration.get("strata", {})
    key = stratum_for(row, stratify_by)
    stratum = strata.get(key) or strata.get(DEFAULT_STRATUM)
    if stratum and stratum.get("mapping"):
        return stratum["mapping"]
    # Fall back to any stratum with a non-empty mapping so we always
    # return a usable curve, even for rows whose stratum has no data.
    for candidate in strata.values():
        if candidate.get("mapping"):
            return candidate["mapping"]
    return []


def apply_calibration(calibration: dict, rows: list[dict[str, str]]) -> Iterable[dict[str, str]]:
    low = calibration["bands"]["low"]
    high = calibration["bands"]["high"]
    for row in rows:
        mapping = lookup_mapping(calibration, row)
        cal = interpolate(mapping, float(row["confidence"]))
        out = dict(row)
        out["confidence_calibrated"] = f"{cal:.6f}"
        out["risk_band"] = risk_band(cal, low, high)
        yield out


def write_enriched_tsv(path: str, rows: Iterable[dict[str, str]], fieldnames: list[str]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_fit_markdown(path: str, calibration: dict) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("w", encoding="utf-8") as handle:
        handle.write("# Confidence Calibration Fit\n\n")
        handle.write(f"- Method: `{calibration['method']}`\n")
        handle.write(f"- Stratify by: `{calibration.get('stratify_by') or '(none)'}`\n")
        handle.write(f"- Fit rows: {calibration['fit_rows']}\n")
        handle.write(f"- Error-involved rows: {calibration['error_involved_rows']}\n")
        handle.write(f"- Bands: low ≥ {calibration['bands']['low']}; high < {calibration['bands']['high']}\n\n")
        for name, stratum in sorted(calibration.get("strata", {}).items()):
            handle.write(f"## Stratum `{name}`\n\n")
            handle.write(f"- Rows: {stratum.get('fit_rows', 0)}\n")
            handle.write(f"- Error-involved: {stratum.get('error_involved_rows', 0)}\n")
            handle.write(f"- Bins: {stratum.get('bins', 0)}\n\n")
            if not stratum.get("raw_bins"):
                handle.write("No rows in this stratum.\n\n")
                continue
            handle.write("| Bin center | Empirical accuracy | Isotonic calibrated | Count |\n")
            handle.write("|---:|---:|---:|---:|\n")
            for b in stratum["raw_bins"]:
                handle.write(f"| {b['center']:.4f} | {b['mean']:.4f} | {b['calibrated']:.4f} | {b['count']} |\n")
            handle.write("\n")


def write_transfer_markdown(path: str, calibration: dict, rows: list[dict[str, str]]) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    gold_rows = [row for row in rows if row.get("gold")]
    if not gold_rows:
        with Path(path).open("w", encoding="utf-8") as handle:
            handle.write("# Calibration Transfer\n\nNo gold labels in input; reliability cannot be measured.\n")
        return
    error_ids = error_involved_ids(gold_rows)
    stratify_by = calibration.get("stratify_by", "")
    low = calibration["bands"]["low"]
    high = calibration["bands"]["high"]

    # Per-decile accuracy per stratum.
    strata_buckets: dict[str, dict[int, list[float]]] = defaultdict(lambda: defaultdict(list))
    band_counts: dict[str, dict[str, dict[str, int]]] = defaultdict(lambda: defaultdict(lambda: {"count": 0, "errors": 0}))
    for row in gold_rows:
        mapping = lookup_mapping(calibration, row)
        cal = interpolate(mapping, float(row["confidence"]))
        is_correct = 0.0 if row["eval_id"] in error_ids else 1.0
        decile = min(int(cal * 10), 9)
        stratum = stratum_for(row, stratify_by)
        strata_buckets[stratum][decile].append(is_correct)
        band = risk_band(cal, low, high)
        band_counts[stratum][band]["count"] += 1
        if row["eval_id"] in error_ids:
            band_counts[stratum][band]["errors"] += 1

    with Path(path).open("w", encoding="utf-8") as handle:
        handle.write("# Calibration Transfer Report\n\n")
        handle.write(f"- Applied calibration fit on {calibration['fit_rows']} rows.\n")
        handle.write(f"- Transfer set: {len(gold_rows)} rows with gold labels ({len(error_ids)} error-involved).\n")
        handle.write(f"- Stratify by: `{stratify_by or '(none)'}`\n\n")
        for name in sorted(strata_buckets.keys()):
            handle.write(f"## Stratum `{name}`\n\n")
            handle.write("### Empirical accuracy by calibrated decile\n\n")
            handle.write("| Decile | Calibrated range | N | Empirical accuracy |\n")
            handle.write("|---:|---|---:|---:|\n")
            for d in range(10):
                vals = strata_buckets[name].get(d, [])
                if not vals:
                    continue
                acc = sum(vals) / len(vals)
                handle.write(f"| {d} | [{d/10:.1f}, {(d+1)/10:.1f}) | {len(vals)} | {acc:.4f} |\n")
            handle.write("\n### Risk-band distribution\n\n")
            handle.write("| Band | N | Errors | Empirical error rate |\n")
            handle.write("|---|---:|---:|---:|\n")
            for band in ("low", "medium", "high"):
                stats = band_counts[name].get(band, {"count": 0, "errors": 0})
                rate = (stats["errors"] / stats["count"]) if stats["count"] else 0.0
                handle.write(f"| {band} | {stats['count']} | {stats['errors']} | {rate:.4f} |\n")
            handle.write("\n")


def main() -> None:
    args = parse_args()
    if args.mode == "fit":
        rows = read_predictions(args.predictions)
        calibration = fit_calibration(rows, args.bins, args.band_low, args.band_high, args.stratify_by)
        Path(args.out_json).parent.mkdir(parents=True, exist_ok=True)
        with Path(args.out_json).open("w", encoding="utf-8") as handle:
            json.dump(calibration, handle, indent=2)
        if args.out_md:
            write_fit_markdown(args.out_md, calibration)
    elif args.mode == "apply":
        with Path(args.calibration).open("r", encoding="utf-8") as handle:
            calibration = json.load(handle)
        rows = read_predictions(args.predictions)
        out_rows = list(apply_calibration(calibration, rows))
        fieldnames = list(rows[0].keys()) if rows else list(REQUIRED_COLUMNS)
        for extra in ("confidence_calibrated", "risk_band"):
            if extra not in fieldnames:
                fieldnames.append(extra)
        write_enriched_tsv(args.out_tsv, out_rows, fieldnames)
        if args.out_md:
            write_transfer_markdown(args.out_md, calibration, rows)


if __name__ == "__main__":
    main()
