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
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Iterable


REQUIRED_COLUMNS = {"eval_id", "dataset", "concept", "gold", "pred", "confidence"}
STRATIFY_CHOICES = ("", "cluster_size", "cluster_size_support")
ERROR_LABEL_CHOICES = ("pairwise", "form-local")
DEFAULT_STRATUM = "_all"
SINGLETON_STRATUM = "singleton"
MULTI_STRATUM = "multi"
SUPPORT_TIERS = 4  # quartiles when --stratify-by cluster_size_support


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)

    fit = sub.add_parser("fit", help="Fit a calibration from a predictions TSV.")
    fit.add_argument("--predictions", required=True, help="Prediction TSV from scripts/run_cognator_eval_table.py --pred-out")
    fit.add_argument("--out-json", required=True, help="Calibration JSON output path")
    fit.add_argument("--out-md", default="", help="Optional reliability-report markdown path")
    fit.add_argument("--bins", type=int, default=20, help="Number of equal-count bins per stratum (default 20)")
    fit.add_argument("--band-low", type=float, default=0.90, help="Calibrated value at or above which risk_band is 'low' (default 0.90)")
    fit.add_argument("--band-high", type=float, default=0.60, help="Calibrated value below which risk_band is 'high' (default 0.60)")
    fit.add_argument(
        "--stratify-by",
        choices=STRATIFY_CHOICES,
        default="",
        help=(
            "Partition fit rows before PAV: '' (single fit), 'cluster_size' "
            "(singleton vs multi-member cluster), or 'cluster_size_support' "
            "(singleton + 4 assigned_support quartiles of the multi stratum; "
            "quartile cut points computed on the fit set and stored in the JSON)."
        ),
    )
    fit.add_argument(
        "--error-label",
        choices=ERROR_LABEL_CHOICES,
        default="pairwise",
        help=(
            "How to label a form as correct/incorrect: 'pairwise' (default) "
            "= not involved in any same-concept false-merge or false-split pair; "
            "'form-local' = B-cubed-style form-level precision AND recall both at or "
            "above 0.5 against the form's gold class within its concept. The pairwise "
            "label is concept-level and pessimistic; form-local is node-local and matches "
            "the scale of node-level confidence metrics. True-singleton gold classes are "
            "vacuously correct under form-local."
        ),
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
    """Concept-level pairwise error-involvement label.

    A form is flagged if at least one same-concept pair containing it
    is a false merge (gold differs, pred same) or false split (gold
    same, pred differs). This propagates concept-level errors to every
    member — pessimistic when the concept has many forms.
    """
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


def form_local_error_ids(rows: list[dict[str, str]], threshold: float = 0.5) -> set[str]:
    """Form-local B-cubed-style correctness label.

    For each form f with gold class G and predicted cluster P inside a
    concept, define:

        precision(f) = |P ∩ gold_peers_of(f)| / max(1, |P|)
        recall(f)    = |P ∩ gold_peers_of(f)| / max(1, |G_peers|)

    where P and G_peers exclude f itself, and the max() guards against
    the "singleton whose gold class is a true singleton in the concept"
    case (both sets empty → both ratios are vacuously 1.0).

    A form is flagged as an error iff precision(f) < threshold OR
    recall(f) < threshold. At threshold=0.5 this collapses to: the
    majority of my cluster-mates must share my gold class AND at
    least half of my gold-peers must be in my cluster. Singletons in
    true-singleton gold classes are correct; singletons whose gold
    class has other members in the concept are errors (false splits).
    Multi-member clusters are correct iff they roughly match their
    dominant gold class's extent.
    """
    ids: set[str] = set()
    by_concept: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row["gold"]:
            by_concept[(row["dataset"], row["concept"])].append(row)
    for members in by_concept.values():
        gold_by_class: dict[str, set[str]] = defaultdict(set)
        pred_by_cluster: dict[str, set[str]] = defaultdict(set)
        for m in members:
            gold_by_class[m["gold"]].add(m["eval_id"])
            pred_by_cluster[m["pred"]].add(m["eval_id"])
        for m in members:
            eid = m["eval_id"]
            gold_peers = gold_by_class[m["gold"]] - {eid}
            pred_peers = pred_by_cluster[m["pred"]] - {eid}
            inter = len(gold_peers & pred_peers)
            precision = inter / max(1, len(pred_peers))
            recall = inter / max(1, len(gold_peers))
            # Vacuously correct when the form is truly alone in both.
            if not pred_peers and not gold_peers:
                continue
            if precision < threshold or recall < threshold:
                ids.add(eid)
    return ids


def labeled_error_ids(rows: list[dict[str, str]], method: str) -> set[str]:
    if method == "pairwise":
        return error_involved_ids(rows)
    if method == "form-local":
        return form_local_error_ids(rows)
    raise ValueError(f"unknown error-label method {method!r}")


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


def multi_support_tier(support: float, quartiles: list[float]) -> str:
    """Return multi_q{1..K} for a support value given K-1 cut points."""
    for i, cut in enumerate(quartiles):
        if support <= cut:
            return f"{MULTI_STRATUM}_q{i+1}"
    return f"{MULTI_STRATUM}_q{len(quartiles)+1}"


def stratum_for(row: dict[str, str], stratify_by: str, support_quartiles: list[float] | None = None) -> str:
    if stratify_by == "":
        return DEFAULT_STRATUM
    size = int(row.get("cluster_size", "1") or "1")
    if stratify_by == "cluster_size":
        return SINGLETON_STRATUM if size <= 1 else MULTI_STRATUM
    if stratify_by == "cluster_size_support":
        if size <= 1:
            return SINGLETON_STRATUM
        if support_quartiles is None:
            raise ValueError("cluster_size_support stratification requires support_quartiles")
        support = float(row.get("assigned_support", "0") or "0")
        return multi_support_tier(support, support_quartiles)
    raise ValueError(f"unknown stratify_by {stratify_by!r}")


def compute_support_quartiles(rows: list[dict[str, str]]) -> list[float]:
    """Cut points for 4 quantiles from the multi-member fit rows."""
    values = sorted(
        float(row.get("assigned_support", "0") or "0")
        for row in rows
        if int(row.get("cluster_size", "1") or "1") > 1
    )
    if not values:
        return []
    # statistics.quantiles needs n >= 2
    if len(values) < SUPPORT_TIERS:
        # Degenerate: just split at distinct sorted values if any.
        return sorted(set(values))[:-1] or [values[0]]
    return list(statistics.quantiles(values, n=SUPPORT_TIERS, method="inclusive"))


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


def fit_calibration(rows: list[dict[str, str]], bins: int, band_low: float, band_high: float, stratify_by: str, error_label: str) -> dict:
    gold_rows = [row for row in rows if row["gold"]]
    if not gold_rows:
        raise ValueError("no rows carry gold labels; cannot fit a calibration")
    first_row_keys = gold_rows[0].keys() if gold_rows else ()
    if stratify_by == "cluster_size" and "cluster_size" not in first_row_keys:
        raise ValueError("--stratify-by cluster_size requires a cluster_size column in the predictions TSV")
    if stratify_by == "cluster_size_support":
        missing = {"cluster_size", "assigned_support"} - set(first_row_keys)
        if missing:
            raise ValueError(f"--stratify-by cluster_size_support requires columns {sorted(missing)} in the predictions TSV")
    error_ids = labeled_error_ids(gold_rows, error_label)
    support_quartiles: list[float] = []
    if stratify_by == "cluster_size_support":
        support_quartiles = compute_support_quartiles(gold_rows)
    strata_rows: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in gold_rows:
        strata_rows[stratum_for(row, stratify_by, support_quartiles)].append(row)
    strata = {name: fit_stratum(members, error_ids, bins) for name, members in strata_rows.items()}
    out = {
        "method": "isotonic-pav",
        "stratify_by": stratify_by,
        "error_label": error_label,
        "fit_rows": len(gold_rows),
        "error_involved_rows": len(error_ids),
        "bands": {"low": band_low, "high": band_high},
        "strata": strata,
    }
    if support_quartiles:
        out["support_quartiles"] = [round(q, 6) for q in support_quartiles]
    return out


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
    support_quartiles = calibration.get("support_quartiles") or None
    key = stratum_for(row, stratify_by, support_quartiles)
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
        handle.write(f"- Error label: `{calibration.get('error_label', 'pairwise')}`\n")
        handle.write(f"- Stratify by: `{calibration.get('stratify_by') or '(none)'}`\n")
        handle.write(f"- Fit rows: {calibration['fit_rows']}\n")
        handle.write(f"- Error-involved rows: {calibration['error_involved_rows']}\n")
        handle.write(f"- Bands: low ≥ {calibration['bands']['low']}; high < {calibration['bands']['high']}\n")
        if calibration.get("support_quartiles"):
            qs = ", ".join(f"{q:.4f}" for q in calibration["support_quartiles"])
            handle.write(f"- Multi support quartile cut points: {qs}\n")
        handle.write("\n")
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
    error_label = calibration.get("error_label", "pairwise")
    error_ids = labeled_error_ids(gold_rows, error_label)
    stratify_by = calibration.get("stratify_by", "")
    support_quartiles = calibration.get("support_quartiles") or None
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
        stratum = stratum_for(row, stratify_by, support_quartiles)
        strata_buckets[stratum][decile].append(is_correct)
        band = risk_band(cal, low, high)
        band_counts[stratum][band]["count"] += 1
        if row["eval_id"] in error_ids:
            band_counts[stratum][band]["errors"] += 1

    with Path(path).open("w", encoding="utf-8") as handle:
        handle.write("# Calibration Transfer Report\n\n")
        handle.write(f"- Applied calibration fit on {calibration['fit_rows']} rows.\n")
        handle.write(f"- Transfer set: {len(gold_rows)} rows with gold labels ({len(error_ids)} error-involved by `{error_label}` label).\n")
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
        calibration = fit_calibration(rows, args.bins, args.band_low, args.band_high, args.stratify_by, args.error_label)
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
