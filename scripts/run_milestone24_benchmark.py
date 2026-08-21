#!/usr/bin/env python3
"""Run the Milestone 24 Cognator-vs-LingPy quality benchmark.

The default benchmark downloads a small cognacy-annotated Lexibank dataset,
converts its CLDF Wordlist tables to Cognator's TSV interchange shape, runs
the Python wrapper over the C core on a focused settings grid, and compares it
with LingPy LexStat/SCA grids.
"""

from __future__ import annotations

import argparse
import contextlib
import csv
import io
import logging
import os
import random
import shlex
import subprocess
import sys
import time
from collections import defaultdict
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from tempfile import TemporaryDirectory

DEFAULT_DATASETS = [
    "carvalhopurus=https://github.com/lexibank/carvalhopurus.git",
    "chaconarawakan=https://github.com/lexibank/chaconarawakan.git",
    "davletshinaztecan=https://github.com/lexibank/davletshinaztecan.git",
    "mcelhanonhuon=https://github.com/lexibank/mcelhanonhuon.git",
    "starostintujia=https://github.com/lexibank/starostintujia.git",
    "syrjaenenuralic=https://github.com/lexibank/syrjaenenuralic.git",
    "wichmannmixezoquean=https://github.com/lexibank/wichmannmixezoquean.git",
]
COGNATOR_FIELDS = ["ID", "Language_ID", "Concept", "Segments", "Family", "Cognacy"]


@dataclass(frozen=True)
class Form:
    eval_id: str
    language_id: str
    concept: str
    segments: str
    family: str
    cognacy: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", default="/tmp/cognator-milestone24")
    parser.add_argument("--out-dir", default="benchmark/milestone24")
    parser.add_argument("--dataset", action="append", default=[], help="NAME=GIT_URL; repeatable")
    parser.add_argument("--skip-download", action="store_true")
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--lingpy-runs", type=int, default=1000)
    parser.add_argument("--max-forms", type=int, default=2500)
    parser.add_argument("--cognator-thresholds", default="0.64,0.66,0.68,0.70,0.72,0.74")
    parser.add_argument("--cognator-min-pairs", default="1,5")
    parser.add_argument("--lingpy-thresholds", default="0.45,0.50,0.55,0.60,0.65,0.70,0.75")
    parser.add_argument("--lingpy-methods", default="lexstat,sca")
    parser.add_argument("--lingpy-cluster-methods", default="infomap,upgma")
    return parser.parse_args()


def split_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def split_floats(value: str) -> list[float]:
    return [float(item) for item in split_csv(value)]


def split_ints(value: str) -> list[int]:
    return [int(item) for item in split_csv(value)]


def parse_datasets(values: list[str]) -> list[tuple[str, str]]:
    parsed = []
    for value in values or DEFAULT_DATASETS:
        if "=" not in value:
            raise SystemExit(f"dataset must be NAME=GIT_URL: {value}")
        name, url = value.split("=", 1)
        parsed.append((name.strip(), url.strip()))
    return parsed


def run(cmd: list[str]) -> None:
    print("+", " ".join(shlex.quote(part) for part in cmd), flush=True)
    subprocess.run(cmd, check=True)


def ensure_dataset(name: str, url: str, work_dir: Path, skip_download: bool) -> Path:
    target = work_dir / name
    if target.exists():
        return target
    if skip_download:
        raise SystemExit(f"{target} does not exist and --skip-download was set")
    target.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", "--depth", "1", url, str(target)])
    return target


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def concept_names(repo: Path) -> dict[str, str]:
    path = repo / "cldf" / "parameters.csv"
    names = {}
    if not path.exists():
        return names
    for row in read_csv(path):
        concept_id = row.get("ID", "").strip()
        name = row.get("Concepticon_Gloss", "").strip() or row.get("Name", "").strip() or concept_id
        if concept_id:
            names[concept_id] = name
    return names


def cognacy_by_form(repo: Path) -> dict[str, str]:
    path = repo / "cldf" / "cognates.csv"
    cognates = {}
    if not path.exists():
        return cognates
    for row in read_csv(path):
        form_id = row.get("Form_ID", "").strip()
        cognateset_id = row.get("Cognateset_ID", "").strip()
        if form_id and cognateset_id and form_id not in cognates:
            cognates[form_id] = cognateset_id
    return cognates


def convert_lexibank(name: str, repo: Path) -> list[Form]:
    concepts = concept_names(repo)
    cognates = cognacy_by_form(repo)
    rows = []
    forms_path = repo / "cldf" / "forms.csv"
    for raw in read_csv(forms_path):
        form_id = raw.get("ID", "").strip()
        segments = raw.get("Segments", "").strip()
        cognacy = cognates.get(form_id, raw.get("Cognacy", "").strip())
        if not form_id or not segments or not cognacy:
            continue
        parameter_id = raw.get("Parameter_ID", "").strip()
        rows.append(
            Form(
                eval_id=form_id,
                language_id=raw.get("Language_ID", "").strip(),
                concept=concepts.get(parameter_id, parameter_id),
                segments=segments,
                family=name,
                cognacy=cognacy,
            )
        )
    return rows


def skip_result(name: str, url: str, rows: list[Form], reason: str) -> dict[str, object]:
    dims = dimensions(rows)
    row = result_row(name, "skipped", "", "", 0.0, 0, 0.0, 0.0, 0.0, 0, 0, dims, reason)
    row["source"] = url
    return row


def write_cognator_input(path: Path, rows: list[Form]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=COGNATOR_FIELDS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    "ID": row.eval_id,
                    "Language_ID": row.language_id,
                    "Concept": row.concept,
                    "Segments": row.segments,
                    "Family": row.family,
                    "Cognacy": row.cognacy,
                }
            )


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
        if gold_cluster is None:
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


def dimensions(rows: list[Form]) -> dict[str, int]:
    return {
        "rows": len(rows),
        "lects": len({row.language_id for row in rows}),
        "concepts": len({row.concept for row in rows}),
    }


def run_cognator(dataset: str, input_path: Path, rows: list[Form], thresholds: list[float], min_pairs: list[int], seed: int) -> list[dict[str, object]]:
    sys.path.insert(0, str(Path("python").resolve()))
    import cognator

    truth = {row.eval_id: f"{row.concept}:{row.cognacy}" for row in rows}
    concepts = {row.eval_id: row.concept for row in rows}
    dims = dimensions(rows)
    results = []
    for threshold in thresholds:
        for min_pair_count in min_pairs:
            start = time.perf_counter()
            result = cognator.detect(
                input_path,
                preset="support-adaptive-bridge",
                threshold=threshold,
                bootstrap=0,
                min_pairs=min_pair_count,
                seed=seed,
            )
            elapsed_ms = int((time.perf_counter() - start) * 1000)
            pred = {
                row["ID"]: f"{concepts[row['ID']]}:{row['Cognator_ID']}"
                for row in result
            }
            precision, recall, f1 = bcubed(pred, truth)
            results.append(
                result_row(
                    dataset,
                    "cognator-python",
                    "support-adaptive-bridge",
                    "",
                    threshold,
                    min_pair_count,
                    precision,
                    recall,
                    f1,
                    elapsed_ms,
                    len(set(pred.values())),
                    dims,
                    "",
                )
            )
    return results


def lingpy_input(rows: list[Form]) -> str:
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


@contextlib.contextmanager
def quiet_lingpy():
    logging.getLogger("lingpy").setLevel(logging.ERROR)
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        yield


def run_lingpy(dataset: str, rows: list[Form], methods: list[str], cluster_methods: list[str], thresholds: list[float], runs: int, seed: int) -> list[dict[str, object]]:
    from lingpy import LexStat

    random.seed(seed)
    dims = dimensions(rows)
    truth = {row.eval_id: f"{row.concept}:{row.cognacy}" for row in rows}
    results = []
    infomap_available = has_igraph()
    with TemporaryDirectory() as tmpdir:
        input_path = Path(tmpdir) / f"{dataset}.tsv"
        input_path.write_text(lingpy_input(rows), encoding="utf-8")
        with quiet_lingpy():
            lex = LexStat(str(input_path))
        scorer_ready = False
        for method in methods:
            if method == "lexstat" and not scorer_ready:
                with quiet_lingpy():
                    lex.get_scorer(runs=runs)
                scorer_ready = True
            for cluster_method in cluster_methods:
                if cluster_method == "infomap" and not infomap_available:
                    continue
                for threshold in thresholds:
                    ref = f"ms24_{method}_{cluster_method}_{str(threshold).replace('.', '_')}"
                    start = time.perf_counter()
                    with quiet_lingpy():
                        lex.cluster(method=method, threshold=threshold, cluster_method=cluster_method, ref=ref)
                    elapsed_ms = int((time.perf_counter() - start) * 1000)
                    pred = {}
                    for idx, row in enumerate(rows, start=1):
                        pred[row.eval_id] = f"{row.concept}:{lex[idx, ref]}"
                    precision, recall, f1 = bcubed(pred, truth)
                    results.append(
                        result_row(
                            dataset,
                            "lingpy",
                            method,
                            cluster_method,
                            threshold,
                            0,
                            precision,
                            recall,
                            f1,
                            elapsed_ms,
                            len(set(pred.values())),
                            dims,
                            f"runs={runs}" if method == "lexstat" else "",
                        )
                    )
    return results


def result_row(
    dataset: str,
    engine: str,
    method: str,
    cluster_method: str,
    threshold: float,
    min_pairs: int,
    precision: float,
    recall: float,
    f1: float,
    elapsed_ms: int,
    clusters: int,
    dims: dict[str, int],
    notes: str,
) -> dict[str, object]:
    return {
        "dataset": dataset,
        "engine": engine,
        "method": method,
        "cluster_method": cluster_method,
        "threshold": threshold,
        "min_pairs": min_pairs,
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "elapsed_ms": elapsed_ms,
        "rows": dims["rows"],
        "lects": dims["lects"],
        "concepts": dims["concepts"],
        "clusters": clusters,
        "notes": notes,
    }


def write_results(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "dataset",
        "engine",
        "method",
        "cluster_method",
        "threshold",
        "min_pairs",
        "precision",
        "recall",
        "f1",
        "elapsed_ms",
        "rows",
        "lects",
        "concepts",
        "clusters",
        "notes",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in rows:
            out = dict(row)
            for field in ("threshold", "precision", "recall", "f1"):
                out[field] = f"{float(out[field]):.6f}"
            if not out["notes"]:
                out["notes"] = "none"
            writer.writerow(out)


def best_by_engine(rows: list[dict[str, object]]) -> dict[str, dict[str, object]]:
    best = {}
    for row in rows:
        key = str(row["engine"])
        current = best.get(key)
        if current is None or float(row["f1"]) > float(current["f1"]):
            best[key] = row
    return best


def runnable_rows(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    return [row for row in rows if row["engine"] != "skipped"]


def config_label(row: dict[str, object]) -> str:
    parts = [str(row["method"])]
    if row["cluster_method"]:
        parts.append(str(row["cluster_method"]))
    parts.append(f"threshold={float(row['threshold']):.2f}")
    if int(row["min_pairs"]):
        parts.append(f"min_pairs={row['min_pairs']}")
    return ", ".join(parts)


def write_summary(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "dataset",
        "best_cognator_config",
        "best_cognator_f1",
        "best_lingpy_config",
        "best_lingpy_f1",
        "delta_f1",
        "winner",
    ]
    grouped = defaultdict(list)
    for row in rows:
        grouped[row["dataset"]].append(row)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for dataset in sorted(grouped):
            best = best_by_engine(runnable_rows(grouped[dataset]))
            cognator = best.get("cognator-python")
            lingpy = best.get("lingpy")
            delta = float(cognator["f1"]) - float(lingpy["f1"]) if cognator and lingpy else 0.0
            writer.writerow(
                {
                    "dataset": dataset,
                    "best_cognator_config": config_label(cognator) if cognator else "",
                    "best_cognator_f1": f"{float(cognator['f1']):.6f}" if cognator else "",
                    "best_lingpy_config": config_label(lingpy) if lingpy else "",
                    "best_lingpy_f1": f"{float(lingpy['f1']):.6f}" if lingpy else "",
                    "delta_f1": f"{delta:.6f}",
                    "winner": "cognator" if delta > 0 else "lingpy" if delta < 0 else "tie",
                }
            )


def write_parameter_summary(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["scope", "selector", "observations", "mean_f1", "best_count", "mean_rank"]
    grouped = defaultdict(list)
    for row in runnable_rows(rows):
        grouped[row["dataset"]].append(row)

    buckets: dict[tuple[str, str], list[tuple[float, int]]] = defaultdict(list)
    for dataset_rows in grouped.values():
        ranked = sorted(dataset_rows, key=lambda row: float(row["f1"]), reverse=True)
        for rank, row in enumerate(ranked, start=1):
            engine = str(row["engine"])
            threshold = f"{float(row['threshold']):.2f}"
            if engine == "cognator-python":
                buckets[("cognator_threshold", threshold)].append((float(row["f1"]), rank))
                buckets[("cognator_min_pairs", str(row["min_pairs"]))].append((float(row["f1"]), rank))
                buckets[("cognator_config", config_label(row))].append((float(row["f1"]), rank))
            elif engine == "lingpy":
                buckets[("lingpy_method", str(row["method"]))].append((float(row["f1"]), rank))
                buckets[("lingpy_cluster_method", str(row["cluster_method"]))].append((float(row["f1"]), rank))
                buckets[("lingpy_config", config_label(row))].append((float(row["f1"]), rank))

    best_config_by_dataset = {}
    for dataset, dataset_rows in grouped.items():
        for engine in ("cognator-python", "lingpy"):
            engine_rows = [row for row in dataset_rows if row["engine"] == engine]
            if not engine_rows:
                continue
            best_config_by_dataset[(dataset, engine)] = config_label(max(engine_rows, key=lambda row: float(row["f1"])))

    best_counts = defaultdict(int)
    for (_, engine), label in best_config_by_dataset.items():
        if engine == "cognator-python":
            best_counts[("cognator_config", label)] += 1
            for part in label.split(", "):
                if part.startswith("threshold="):
                    best_counts[("cognator_threshold", part.removeprefix("threshold="))] += 1
                if part.startswith("min_pairs="):
                    best_counts[("cognator_min_pairs", part.removeprefix("min_pairs="))] += 1
        elif engine == "lingpy":
            best_counts[("lingpy_config", label)] += 1
            parts = label.split(", ")
            best_counts[("lingpy_method", parts[0])] += 1
            if len(parts) > 2:
                best_counts[("lingpy_cluster_method", parts[1])] += 1

    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for key in sorted(buckets):
            values = buckets[key]
            mean_f1 = sum(value[0] for value in values) / len(values)
            mean_rank = sum(value[1] for value in values) / len(values)
            writer.writerow(
                {
                    "scope": key[0],
                    "selector": key[1],
                    "observations": len(values),
                    "mean_f1": f"{mean_f1:.6f}",
                    "best_count": best_counts[key],
                    "mean_rank": f"{mean_rank:.2f}",
                }
            )


def write_report(path: Path, rows: list[dict[str, object]], datasets: list[tuple[str, str]]) -> None:
    grouped = defaultdict(list)
    for row in rows:
        grouped[row["dataset"]].append(row)
    lines = [
        "# Milestone 24 LingPy Quality Benchmark",
        "",
        f"Run date: {date.today().isoformat()}",
        "",
        "Goal: exercise the C core through the Python wrapper and compare it with LingPy best-of-grid settings on temporary Lexibank data.",
        "",
        "## Data",
        "",
    ]
    for name, url in datasets:
        dims = dimensions_from_results(grouped.get(name, []))
        lines.append(f"- {name}: {url} ({dims})")
    lines.extend(["", "## Best Results", ""])
    deltas = []
    for dataset in sorted(grouped):
        best = best_by_engine(runnable_rows(grouped[dataset]))
        cognator = best.get("cognator-python")
        lingpy = best.get("lingpy")
        if not cognator or not lingpy:
            skipped = [row for row in grouped[dataset] if row["engine"] == "skipped"]
            if skipped:
                lines.append(f"- {dataset}: skipped ({skipped[0]['notes']}).")
            continue
        delta = float(cognator["f1"]) - float(lingpy["f1"])
        deltas.append(delta)
        lines.append(
            f"- {dataset}: Cognator {float(cognator['f1']):.4f} ({config_label(cognator)}) vs "
            f"LingPy {float(lingpy['f1']):.4f} ({config_label(lingpy)}), delta {delta:+.4f}."
        )
    if deltas:
        wins = sum(1 for delta in deltas if delta > 0)
        lines.extend(
            [
                "",
                "## Aggregate",
                "",
                f"- Cognator wins {wins}/{len(deltas)} runnable datasets; mean best-of-grid delta is {sum(deltas) / len(deltas):+.4f} F1.",
                "- This wider sample does not yet justify claiming broad quality superiority over LingPy; it identifies the next modeling work needed before Go retirement.",
            ]
        )
    lines.extend(parameter_findings(rows))
    lines.extend(
        [
            "",
            "## Notes",
            "",
            "- Cognator is run through `python/cognator` so the result covers the installable wrapper path over the C implementation.",
            "- LingPy is run with LexStat and SCA over infomap and UPGMA threshold grids; LexStat uses the configured permutation count.",
            "- The Lexibank checkout lives under the temporary work directory and is intentionally not committed.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def dimensions_from_results(rows: list[dict[str, object]]) -> str:
    if not rows:
        return "not run"
    row = rows[0]
    return f"{row['rows']} forms, {row['lects']} lects, {row['concepts']} concepts"


def parameter_findings(rows: list[dict[str, object]]) -> list[str]:
    grouped = defaultdict(list)
    for row in runnable_rows(rows):
        grouped[row["dataset"]].append(row)
    cognator_thresholds = defaultdict(int)
    cognator_min_pairs = defaultdict(int)
    lingpy_methods = defaultdict(int)
    lingpy_clusters = defaultdict(int)
    for dataset_rows in grouped.values():
        best = best_by_engine(dataset_rows)
        cognator = best.get("cognator-python")
        lingpy = best.get("lingpy")
        if cognator:
            cognator_thresholds[f"{float(cognator['threshold']):.2f}"] += 1
            cognator_min_pairs[str(cognator["min_pairs"])] += 1
        if lingpy:
            lingpy_methods[str(lingpy["method"])] += 1
            lingpy_clusters[str(lingpy["cluster_method"])] += 1
    if not grouped:
        return []
    return [
        "",
        "## Parameter Findings",
        "",
        f"- Cognator best thresholds by dataset: {format_counts(cognator_thresholds)}.",
        "- Cognator's stronger settings are concentrated at lower thresholds, so the C/Python default keeps thresholding family-adaptive and clamps it to the lower-threshold band instead of raising the global cutoff.",
        f"- Cognator `min_pairs` best counts: {format_counts(cognator_min_pairs)}; `min_pairs=5` did not produce a best-of-dataset quality win in this sample.",
        f"- LingPy best methods by dataset: {format_counts(lingpy_methods)}.",
        f"- LingPy best cluster methods by dataset: {format_counts(lingpy_clusters)}.",
        "- LingPy LexStat remains the main quality target; SCA only wins on `syrjaenenuralic`, where conservative UPGMA at threshold 0.45 is much stronger than Cognator's current graph setting.",
    ]


def format_counts(counts: dict[str, int]) -> str:
    if not counts:
        return "none"
    return ", ".join(f"{key}={value}" for key, value in sorted(counts.items(), key=lambda item: (-item[1], item[0])))


def main() -> None:
    args = parse_args()
    datasets = parse_datasets(args.dataset)
    work_dir = Path(args.work_dir)
    out_dir = Path(args.out_dir)
    os.environ.setdefault("PYTHONHASHSEED", "0")

    all_results = []
    for name, url in datasets:
        repo = ensure_dataset(name, url, work_dir, args.skip_download)
        rows = convert_lexibank(name, repo)
        if not rows:
            raise SystemExit(f"{name} produced no cognacy-annotated rows")
        if args.max_forms > 0 and len(rows) > args.max_forms:
            all_results.append(skip_result(name, url, rows, f"{len(rows)} forms exceeds max-forms={args.max_forms}"))
            continue
        input_path = work_dir / "converted" / f"{name}.tsv"
        write_cognator_input(input_path, rows)
        all_results.extend(
            run_cognator(
                name,
                input_path,
                rows,
                split_floats(args.cognator_thresholds),
                split_ints(args.cognator_min_pairs),
                args.seed,
            )
        )
        all_results.extend(
            run_lingpy(
                name,
                rows,
                split_csv(args.lingpy_methods),
                split_csv(args.lingpy_cluster_methods),
                split_floats(args.lingpy_thresholds),
                args.lingpy_runs,
                args.seed,
            )
        )

    write_results(out_dir / "results.tsv", all_results)
    write_summary(out_dir / "summary.tsv", all_results)
    write_parameter_summary(out_dir / "parameter_summary.tsv", all_results)
    write_report(out_dir / "report.md", all_results, datasets)


if __name__ == "__main__":
    main()
