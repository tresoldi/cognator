#!/usr/bin/env python3
"""Generate paper figures and tables from cognator benchmark artifacts.

One command produces every figure and table in the paper, from a set
of canonical inputs. Designed to be re-run end-to-end whenever the
benchmark is re-executed (e.g. when a new arcaverborum snapshot lands).

Inputs (all optional; figures only drawn if required inputs are present):
  --predictions-devcore: Dev-Core predictions TSV (scripts/run_cognator_eval_table.py --pred-out)
  --predictions-devfull: Dev-Full predictions TSV
  --predictions-holdout: Holdout predictions TSV
  --calibration:         Calibration JSON (scripts/calibrate_confidence.py fit)
  --lingpy-devcore:      LingPy results TSV for Dev-Core (per-family F)
  --lingpy-devfull:      ... Dev-Full
  --lingpy-holdout:      ... Holdout
  --cognator-devfull:    Cognator results TSV for Dev-Full (per-family F)
  --cognator-devcore:    ... Dev-Core
  --cognator-holdout:    ... Holdout

Outputs (PDFs + matching markdown tables) land in --out-dir:
  reliability_by_stratum.{pdf,md}
  risk_band_transfer.{pdf,md}
  family_f_comparison.{pdf,md}
  runtime_comparison.{pdf,md}

Dependencies: matplotlib + stdlib. No numpy, no pandas, no seaborn —
the figures are intentionally simple and the dependency surface is kept
tight for reproducibility across reviewers' setups.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

# matplotlib is installed into the project venv; fail loudly if absent.
try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover
    raise SystemExit("matplotlib required; install into project venv first") from exc


# -----------------------------------------------------------------------------
# Shared helpers (duplicate minimal surface of calibrate_confidence.py rather
# than importing, so paper_figures.py stays a self-contained reproducibility
# artifact).
# -----------------------------------------------------------------------------

SINGLETON = "singleton"


def read_tsv(path: str) -> list[dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def error_involved_form_local(rows: list[dict[str, str]]) -> set[str]:
    """Form-local B-cubed correctness label (matches calibrate_confidence.py)."""
    ids: set[str] = set()
    by_concept: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row.get("gold"):
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
            if not pred_peers and not gold_peers:
                continue
            precision = inter / max(1, len(pred_peers))
            recall = inter / max(1, len(gold_peers))
            if precision < 0.5 or recall < 0.5:
                ids.add(eid)
    return ids


def stratum_for(row: dict[str, str], quartiles: list[float]) -> str:
    size = int(row.get("cluster_size", "1") or "1")
    if size <= 1:
        return SINGLETON
    support = float(row.get("assigned_support", "0") or "0")
    for i, cut in enumerate(quartiles):
        if support <= cut:
            return f"multi_q{i + 1}"
    return f"multi_q{len(quartiles) + 1}"


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


def risk_band(calibrated: float, low: float = 0.90, high: float = 0.60) -> str:
    if calibrated >= low:
        return "low"
    if calibrated < high:
        return "high"
    return "medium"


def stratum_order(calibration: dict) -> list[str]:
    """Stable order for stratum panels: singleton, multi_q1..qN."""
    strata = list(calibration.get("strata", {}).keys())
    out: list[str] = []
    if SINGLETON in strata:
        out.append(SINGLETON)
    for s in sorted(s for s in strata if s != SINGLETON):
        out.append(s)
    return out


# -----------------------------------------------------------------------------
# Figure 1: reliability by stratum
# -----------------------------------------------------------------------------


def figure_reliability_by_stratum(
    holdout_rows: list[dict[str, str]],
    calibration: dict,
    out_pdf: str,
    out_md: str,
) -> None:
    """One panel per stratum; empirical accuracy vs calibrated-confidence decile,
    with the y=x reference line and bin counts annotated.
    """
    error_ids = error_involved_form_local([r for r in holdout_rows if r.get("gold")])
    quartiles = calibration.get("support_quartiles") or []
    strata = stratum_order(calibration)

    # Bin each row's calibrated value by decile per stratum.
    per_stratum: dict[str, dict[int, list[float]]] = defaultdict(lambda: defaultdict(list))
    for row in holdout_rows:
        if not row.get("gold"):
            continue
        stratum = stratum_for(row, quartiles)
        mapping = calibration["strata"].get(stratum, {}).get("mapping", [])
        if not mapping:
            # Fall back to any mapping so the figure still shows something
            for name in strata:
                if calibration["strata"].get(name, {}).get("mapping"):
                    mapping = calibration["strata"][name]["mapping"]
                    break
        cal = interpolate(mapping, float(row["confidence"]))
        decile = min(int(cal * 10), 9)
        is_correct = 0.0 if row["eval_id"] in error_ids else 1.0
        per_stratum[stratum][decile].append(is_correct)

    n_panels = len(strata)
    cols = min(3, n_panels)
    rows_n = math.ceil(n_panels / cols)
    fig, axes = plt.subplots(rows_n, cols, figsize=(4.5 * cols, 3.5 * rows_n), squeeze=False)

    for idx, stratum in enumerate(strata):
        ax = axes[idx // cols][idx % cols]
        xs, ys, counts = [], [], []
        for decile in range(10):
            bucket = per_stratum[stratum].get(decile, [])
            if len(bucket) < 10:
                continue
            xs.append((decile + 0.5) / 10)
            ys.append(sum(bucket) / len(bucket))
            counts.append(len(bucket))
        ax.plot([0, 1], [0, 1], linestyle="--", color="0.6", linewidth=1, label="perfect")
        if xs:
            sizes = [max(30, min(300, c / 5)) for c in counts]
            ax.scatter(xs, ys, s=sizes, color="C0", alpha=0.85, edgecolors="white", linewidths=0.8)
        ax.set_xlim(-0.02, 1.02)
        ax.set_ylim(-0.02, 1.02)
        ax.set_xlabel("calibrated confidence")
        ax.set_ylabel("empirical accuracy")
        ax.set_title(stratum)
        ax.grid(alpha=0.3)

    # Hide unused panels.
    for idx in range(n_panels, rows_n * cols):
        axes[idx // cols][idx % cols].set_visible(False)

    fig.suptitle("Reliability by stratum (Holdout); marker size ∝ bin count", y=1.02)
    fig.tight_layout()
    Path(out_pdf).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_pdf, bbox_inches="tight")
    plt.close(fig)

    # Markdown table mirror.
    lines = ["| Stratum | Decile | N | Empirical accuracy |", "|---|---:|---:|---:|"]
    for stratum in strata:
        for decile in range(10):
            bucket = per_stratum[stratum].get(decile, [])
            if not bucket:
                continue
            acc = sum(bucket) / len(bucket)
            lines.append(f"| {stratum} | {decile} | {len(bucket)} | {acc:.4f} |")
    Path(out_md).write_text("# Reliability by stratum (Holdout)\n\n" + "\n".join(lines) + "\n", encoding="utf-8")


# -----------------------------------------------------------------------------
# Figure 2: risk-band distribution
# -----------------------------------------------------------------------------


def figure_risk_band_transfer(
    holdout_rows: list[dict[str, str]],
    calibration: dict,
    out_pdf: str,
    out_md: str,
) -> None:
    """Stacked horizontal bar per stratum; segments = low/medium/high with
    row counts and empirical error rates annotated.
    """
    error_ids = error_involved_form_local([r for r in holdout_rows if r.get("gold")])
    quartiles = calibration.get("support_quartiles") or []
    bands_conf = calibration.get("bands", {"low": 0.90, "high": 0.60})
    strata = stratum_order(calibration)

    # Per-stratum per-band {count, errors}.
    per_band: dict[str, dict[str, dict[str, int]]] = defaultdict(
        lambda: {"low": {"n": 0, "err": 0}, "medium": {"n": 0, "err": 0}, "high": {"n": 0, "err": 0}}
    )
    for row in holdout_rows:
        if not row.get("gold"):
            continue
        stratum = stratum_for(row, quartiles)
        mapping = calibration["strata"].get(stratum, {}).get("mapping", [])
        if not mapping:
            continue
        cal = interpolate(mapping, float(row["confidence"]))
        band = risk_band(cal, bands_conf["low"], bands_conf["high"])
        per_band[stratum][band]["n"] += 1
        if row["eval_id"] in error_ids:
            per_band[stratum][band]["err"] += 1

    fig, ax = plt.subplots(figsize=(8, max(3, 0.6 * len(strata))))
    colors = {"low": "#2ca02c", "medium": "#ff7f0e", "high": "#d62728"}
    y_positions = list(range(len(strata)))
    for idx, stratum in enumerate(strata):
        left = 0.0
        total = sum(per_band[stratum][b]["n"] for b in ("low", "medium", "high"))
        if total == 0:
            continue
        for band in ("low", "medium", "high"):
            stats = per_band[stratum][band]
            if stats["n"] == 0:
                continue
            width = stats["n"] / total
            rate = stats["err"] / stats["n"] if stats["n"] else 0.0
            ax.barh(idx, width, left=left, color=colors[band], edgecolor="white", linewidth=0.8)
            if width > 0.06:
                ax.text(
                    left + width / 2,
                    idx,
                    f"{band}\nn={stats['n']}\n{rate:.0%} err",
                    ha="center",
                    va="center",
                    fontsize=8,
                    color="white" if band != "medium" else "black",
                )
            left += width

    ax.set_yticks(y_positions)
    ax.set_yticklabels(strata)
    ax.set_xlim(0, 1)
    ax.set_xlabel("fraction of stratum's Holdout rows")
    ax.set_title("Risk-band distribution on Holdout (Dev-Core calibration transferred)")
    ax.grid(axis="x", alpha=0.3)
    fig.tight_layout()
    Path(out_pdf).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_pdf, bbox_inches="tight")
    plt.close(fig)

    # Markdown table mirror.
    lines = ["| Stratum | Band | N | Errors | Empirical error rate |", "|---|---|---:|---:|---:|"]
    for stratum in strata:
        for band in ("low", "medium", "high"):
            stats = per_band[stratum][band]
            rate = (stats["err"] / stats["n"]) if stats["n"] else 0.0
            lines.append(f"| {stratum} | {band} | {stats['n']} | {stats['err']} | {rate:.4f} |")
    Path(out_md).write_text(
        "# Risk-band transfer to Holdout\n\n" + "\n".join(lines) + "\n", encoding="utf-8"
    )


# -----------------------------------------------------------------------------
# Figure 3: per-family F comparison (cognator vs LingPy)
# -----------------------------------------------------------------------------


def figure_family_f_comparison(
    cognator_results: list[dict[str, str]],
    lingpy_results: list[dict[str, str]],
    split_label: str,
    out_pdf: str,
    out_md: str,
) -> None:
    """Grouped horizontal bars; each family has cognator + lingpy F side by side,
    sorted by the cognator-vs-lingpy delta.
    """

    def as_dict(rows: list[dict[str, str]]) -> dict[str, float]:
        out: dict[str, float] = {}
        for r in rows:
            dataset = r.get("dataset") or r.get("Dataset") or r.get("family") or ""
            if not dataset or r.get("skipped"):
                continue
            # accept f1/f/F_score column variations
            for key in ("f1", "f", "F", "f_score", "F_score"):
                if key in r and r[key]:
                    try:
                        out[dataset] = float(r[key])
                    except ValueError:
                        pass
                    break
        return out

    cog = as_dict(cognator_results)
    lingpy = as_dict(lingpy_results)
    families = sorted(set(cog.keys()) & set(lingpy.keys()), key=lambda f: cog[f] - lingpy[f])

    if not families:
        # Empty output — still emit a markdown stub so the run is idempotent.
        Path(out_md).write_text(
            f"# Per-family F comparison ({split_label})\n\nNo overlapping families found.\n", encoding="utf-8"
        )
        return

    y_positions = list(range(len(families)))
    heights = 0.4
    fig, ax = plt.subplots(figsize=(9, max(4, 0.35 * len(families))))
    cog_vals = [cog[f] for f in families]
    lp_vals = [lingpy[f] for f in families]
    ax.barh([y - heights / 2 for y in y_positions], cog_vals, height=heights, label="cognator", color="C0")
    ax.barh([y + heights / 2 for y in y_positions], lp_vals, height=heights, label="LingPy", color="C1")
    ax.set_yticks(y_positions)
    ax.set_yticklabels(families)
    ax.set_xlim(0, 1)
    ax.set_xlabel("B-cubed F1")
    ax.set_title(f"Per-family F1: cognator vs LingPy ({split_label})")
    ax.legend(loc="lower right")
    ax.grid(axis="x", alpha=0.3)
    fig.tight_layout()
    Path(out_pdf).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_pdf, bbox_inches="tight")
    plt.close(fig)

    lines = [
        f"# Per-family F1 comparison ({split_label})",
        "",
        "| Family | Cognator F | LingPy F | Δ |",
        "|---|---:|---:|---:|",
    ]
    for f in families:
        delta = cog[f] - lingpy[f]
        lines.append(f"| {f} | {cog[f]:.4f} | {lingpy[f]:.4f} | {delta:+.4f} |")
    Path(out_md).write_text("\n".join(lines) + "\n", encoding="utf-8")


# -----------------------------------------------------------------------------
# Figure 4: runtime comparison
# -----------------------------------------------------------------------------


def figure_runtime_comparison(
    cognator_results: list[dict[str, str]],
    lingpy_results: list[dict[str, str]],
    split_label: str,
    out_pdf: str,
    out_md: str,
) -> None:
    """Log-scale scatter of per-family wall time: LingPy (x) vs cognator (y).
    Points below y=x are cognator-faster.
    """

    def times_dict(rows: list[dict[str, str]]) -> dict[str, float]:
        out: dict[str, float] = {}
        for r in rows:
            dataset = r.get("dataset") or r.get("Dataset") or r.get("family") or ""
            if not dataset or r.get("skipped"):
                continue
            for key in ("elapsed_ms", "elapsed_millis", "time_ms", "time"):
                if key in r and r[key]:
                    try:
                        out[dataset] = float(r[key]) / 1000.0
                    except ValueError:
                        pass
                    break
        return out

    cog_t = times_dict(cognator_results)
    lp_t = times_dict(lingpy_results)
    families = sorted(set(cog_t.keys()) & set(lp_t.keys()))

    if not families:
        Path(out_md).write_text(
            f"# Runtime comparison ({split_label})\n\nNo overlapping families found.\n", encoding="utf-8"
        )
        return

    fig, ax = plt.subplots(figsize=(6, 6))
    xs = [lp_t[f] for f in families]
    ys = [cog_t[f] for f in families]
    ax.scatter(xs, ys, s=50, color="C0", alpha=0.7, edgecolor="white")
    bound_low = max(1e-2, min(min(xs), min(ys)) * 0.8)
    bound_high = max(max(xs), max(ys)) * 1.2
    ax.plot([bound_low, bound_high], [bound_low, bound_high], color="0.6", linestyle="--", linewidth=1, label="y=x")
    for x10 in (10, 50):
        ax.plot(
            [bound_low, bound_high],
            [bound_low / x10, bound_high / x10],
            color="0.8",
            linestyle=":",
            linewidth=1,
            label=f"{x10}× faster" if x10 == 10 else f"{x10}× faster",
        )
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlim(bound_low, bound_high)
    ax.set_ylim(bound_low, bound_high)
    ax.set_xlabel("LingPy wall time (s, log scale)")
    ax.set_ylabel("cognator wall time (s, log scale)")
    ax.set_title(f"Per-family runtime ({split_label})")
    ax.legend(loc="upper left")
    ax.grid(alpha=0.3, which="both")
    fig.tight_layout()
    Path(out_pdf).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_pdf, bbox_inches="tight")
    plt.close(fig)

    lines = [
        f"# Runtime comparison ({split_label})",
        "",
        "| Family | Cognator (s) | LingPy (s) | Speedup |",
        "|---|---:|---:|---:|",
    ]
    for f in families:
        speedup = lp_t[f] / max(1e-9, cog_t[f])
        lines.append(f"| {f} | {cog_t[f]:.3f} | {lp_t[f]:.3f} | {speedup:.1f}× |")
    Path(out_md).write_text("\n".join(lines) + "\n", encoding="utf-8")


# -----------------------------------------------------------------------------
# Driver
# -----------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--predictions-devcore", default="", help="Dev-Core predictions TSV")
    ap.add_argument("--predictions-devfull", default="", help="Dev-Full predictions TSV")
    ap.add_argument("--predictions-holdout", default="", help="Holdout predictions TSV")
    ap.add_argument("--calibration", default="", help="Calibration JSON from scripts/calibrate_confidence.py fit")
    ap.add_argument("--cognator-devcore", default="", help="Cognator per-family results TSV (Dev-Core)")
    ap.add_argument("--cognator-devfull", default="", help="Cognator per-family results TSV (Dev-Full)")
    ap.add_argument("--cognator-holdout", default="", help="Cognator per-family results TSV (Holdout)")
    ap.add_argument("--lingpy-devcore", default="", help="LingPy per-family results TSV (Dev-Core)")
    ap.add_argument("--lingpy-devfull", default="", help="LingPy per-family results TSV (Dev-Full)")
    ap.add_argument("--lingpy-holdout", default="", help="LingPy per-family results TSV (Holdout)")
    ap.add_argument("--out-dir", required=True, help="Output directory for figures + markdown tables")
    return ap.parse_args()


def main() -> None:
    args = parse_args()
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    calibration = None
    if args.calibration:
        calibration = json.loads(Path(args.calibration).read_text(encoding="utf-8"))

    holdout_rows: list[dict[str, str]] = []
    if args.predictions_holdout:
        holdout_rows = read_tsv(args.predictions_holdout)

    if calibration and holdout_rows:
        figure_reliability_by_stratum(
            holdout_rows,
            calibration,
            out_pdf=str(out / "reliability_by_stratum.pdf"),
            out_md=str(out / "reliability_by_stratum.md"),
        )
        figure_risk_band_transfer(
            holdout_rows,
            calibration,
            out_pdf=str(out / "risk_band_transfer.pdf"),
            out_md=str(out / "risk_band_transfer.md"),
        )
        print(f"wrote {out / 'reliability_by_stratum.pdf'}")
        print(f"wrote {out / 'risk_band_transfer.pdf'}")
    else:
        print("skipped calibration figures (need --calibration and --predictions-holdout)")

    for label, cog_path, lp_path in (
        ("Dev-Core", args.cognator_devcore, args.lingpy_devcore),
        ("Dev-Full", args.cognator_devfull, args.lingpy_devfull),
        ("Holdout", args.cognator_holdout, args.lingpy_holdout),
    ):
        if not (cog_path and lp_path):
            continue
        cog = read_tsv(cog_path)
        lp = read_tsv(lp_path)
        slug = label.lower().replace("-", "_")
        figure_family_f_comparison(
            cog,
            lp,
            split_label=label,
            out_pdf=str(out / f"family_f_{slug}.pdf"),
            out_md=str(out / f"family_f_{slug}.md"),
        )
        figure_runtime_comparison(
            cog,
            lp,
            split_label=label,
            out_pdf=str(out / f"runtime_{slug}.pdf"),
            out_md=str(out / f"runtime_{slug}.md"),
        )
        print(f"wrote family_f and runtime figures for {label}")


if __name__ == "__main__":
    main()
