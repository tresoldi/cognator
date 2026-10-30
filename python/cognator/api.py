from __future__ import annotations

import csv
import json
import tempfile
from collections import defaultdict
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Union

from ._cognator import detect_file, detect_file_pairs

# A row, as it comes from csv.DictReader, from the extension, or from a
# DataFrame's to_dict("records"). Values are read back as strings by
# _record_value, but the extension returns floats and ints, so the value type
# is Any rather than a lie.
Record = Mapping[str, Any]

# Anything _records_from_table_like accepts: a DataFrame or other object with a
# to_dict method, or any iterable of records. Deliberately Any -- narrowing it
# would name pandas, which is not a dependency and must not become one.
TableLike = Any

# Union, not `str | bytes | Path`: this is a runtime assignment, not an
# annotation, so `from __future__ import annotations` does not defer it and
# PEP 604 syntax raises TypeError on the 3.9 this package still supports.
PathLike = Union[str, bytes, Path]


INPUT_COLUMNS = ("ID", "Language_ID", "Concept", "Segments", "Family", "Cognacy")
SIDECAR_COLUMNS = (
    "ID",
    "Cognator_ID",
    "Cognator_Confidence",
    "Cognator_Assigned_Support",
    "Cognator_Alternative_Support",
    "Cognator_Support_Margin",
    "Cognator_Cluster_Size",
    "Cognator_Candidate_Neighbors",
    "Cognator_Bootstrap",
    "Cognator_Bootstrap_Low",
    "Cognator_Bootstrap_High",
)
MORPHEME_IDS_COLUMN = "Cognator_Morpheme_IDs"

# Keyword arguments that select the pairwise export rather than a cg_options
# field. Popped before the rest reach the extension.
_PAIR_OPTIONS = ("pairs", "morpheme_ids", "null_samples", "null_min", "null_seed")


@dataclass(frozen=True)
class EvaluationResult:
    scored_count: int
    pred_count: int
    truth_count: int
    precision: float
    recall: float
    f1: float

    def to_dict(self) -> dict[str, Any]:
        return {
            "scored_count": self.scored_count,
            "pred_count": self.pred_count,
            "truth_count": self.truth_count,
            "precision": self.precision,
            "recall": self.recall,
            "f1": self.f1,
        }


def _records_from_table_like(data: TableLike) -> list[Record]:
    if hasattr(data, "to_dict"):
        try:
            records = data.to_dict("records")
        except TypeError:
            records = data.to_dict(orient="records")
        return list(records)
    return list(data)


def _record_value(record: Any, name: str) -> str:
    # The ternary ruff suggests is a 100-character line whose two branches are
    # the mapping case and the attribute case. They read as two cases here and
    # as punctuation there.
    if hasattr(record, "get"):  # noqa: SIM108
        value = record.get(name, "")
    else:
        value = getattr(record, name, "")
    return "" if value is None else str(value)


def _records_from_path(path: PathLike) -> list[Record]:
    delimiter = "\t" if str(path).endswith(".tsv") else ","
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter=delimiter))


def _records_from_input(
    data: Wordlist | CognateResult | PathLike | TableLike,
) -> list[Record]:
    if isinstance(data, Wordlist):
        return data.records
    if isinstance(data, CognateResult):
        return data.rows
    if isinstance(data, (str, bytes, Path)):
        return _records_from_path(data)
    return _records_from_table_like(data)


def _pairs_unit(pairs: Any, morpheme_ids: bool) -> str | None:
    if pairs is None or pairs is False:
        return "morphemes" if morpheme_ids else None
    if pairs is True:
        return "morphemes" if morpheme_ids else "words"
    if pairs in ("words", "morphemes"):
        return str(pairs)
    raise ValueError(f"pairs must be True, 'words' or 'morphemes', not {pairs!r}")


def _detect_path(path: str, options: Mapping[str, Any]) -> CognateResult:
    options = dict(options)
    pair_options = {name: options.pop(name) for name in _PAIR_OPTIONS if name in options}
    morpheme_ids = bool(pair_options.get("morpheme_ids", False))
    unit = _pairs_unit(pair_options.get("pairs"), morpheme_ids)
    if unit is None:
        # No export asked for: exactly the call this module always made.
        return CognateResult(detect_file(path, **options))
    found = detect_file_pairs(
        path,
        pairs_unit=unit,
        morpheme_ids=morpheme_ids,
        null_samples=pair_options.get("null_samples"),
        null_min=pair_options.get("null_min"),
        null_seed=pair_options.get("null_seed"),
        **options,
    )
    return CognateResult(
        found["rows"],
        pairs=found["pairs"],
        pairs_tsv=found["tsv"],
        pairs_json=found["json"],
    )


def _detect_records(records: TableLike, options: Mapping[str, Any]) -> CognateResult:
    rows = _records_from_table_like(records)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="", suffix=".tsv") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=INPUT_COLUMNS, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        for record in rows:
            writer.writerow({name: _record_value(record, name) for name in INPUT_COLUMNS})
        handle.flush()
        return _detect_path(handle.name, options)


def detect(data: PathLike | TableLike, **options: Any) -> CognateResult:
    """Cluster a wordlist; see docs/python_usage.md for the options.

    ``pairs=True`` (or ``"words"``) also computes the word-level pairwise
    export, ``pairs="morphemes"`` the per-morpheme one, and
    ``morpheme_ids=True`` adds morpheme clusters to every row. The weights are
    background-match rates, not cognacy probabilities: docs/pairs.md.
    """
    if isinstance(data, (str, bytes, Path)):
        path = data.decode() if isinstance(data, bytes) else str(data)
        return _detect_path(path, options)
    return _detect_records(data, options)


def detect_records(records: TableLike, **options: Any) -> CognateResult:
    return _detect_records(records, options)


def detect_pairs(
    data: PathLike | TableLike, unit: str = "words", **options: Any
) -> list[dict[str, Any]]:
    """Only the weighted pairs: ``detect(data, pairs=unit, ...).pairs``."""
    pairs = detect(data, pairs=unit, **options).pairs
    assert pairs is not None
    return pairs


def load(path: PathLike) -> Wordlist:
    return Wordlist.from_file(path)


def evaluate(
    pred: Wordlist | CognateResult | PathLike | TableLike,
    truth: Wordlist | CognateResult | PathLike | TableLike,
    *,
    truth_column: str = "Cognacy",
    per_concept: bool = True,
) -> EvaluationResult:
    pred_rows = _records_from_input(pred)
    truth_rows = _records_from_input(truth)
    pred_assignments: dict[str, str] = {}
    truth_assignments: dict[str, str] = {}

    for row in pred_rows:
        form_id = _record_value(row, "ID")
        cluster = _record_value(row, "Cognator_ID")
        if form_id and cluster:
            pred_assignments[form_id] = cluster

    for row in truth_rows:
        form_id = _record_value(row, "ID")
        gold = _record_value(row, truth_column)
        if not form_id or not gold:
            continue
        if per_concept:
            concept = _record_value(row, "Concept")
            gold = f"{concept}:{gold}"
        truth_assignments[form_id] = gold

    return _bcubed(pred_assignments, truth_assignments)


def _bcubed(pred: Mapping[str, str], truth: Mapping[str, str]) -> EvaluationResult:
    pred_members: dict[str, list[str]] = defaultdict(list)
    truth_members: dict[str, list[str]] = defaultdict(list)
    for form_id, cluster in pred.items():
        pred_members[cluster].append(form_id)
    for form_id, cluster in truth.items():
        truth_members[cluster].append(form_id)

    p_sum = 0.0
    r_sum = 0.0
    scored = 0
    for form_id, pred_cluster in pred.items():
        truth_cluster = truth.get(form_id)
        if truth_cluster is None:
            continue
        pred_cluster_members = [item for item in pred_members[pred_cluster] if item in truth]
        truth_cluster_members = truth_members[truth_cluster]
        shared = sum(1 for item in pred_cluster_members if truth[item] == truth_cluster)
        p_sum += shared / max(1, len(pred_cluster_members))
        r_sum += shared / max(1, len(truth_cluster_members))
        scored += 1
    if scored == 0:
        return EvaluationResult(scored, len(pred), len(truth), 0.0, 0.0, 0.0)
    precision = p_sum / scored
    recall = r_sum / scored
    f1 = 0.0 if precision + recall == 0.0 else 2.0 * precision * recall / (precision + recall)
    return EvaluationResult(scored, len(pred), len(truth), precision, recall, f1)


class CognateResult:
    def __init__(
        self,
        rows: Sequence[Record],
        *,
        pairs: Sequence[dict[str, Any]] | None = None,
        pairs_tsv: str | None = None,
        pairs_json: str | None = None,
    ) -> None:
        self.rows: list[Record] = list(rows)
        self.resolved_threshold = self._resolved_threshold()
        # The pairwise export, when detect was asked for it; None otherwise.
        self.pairs: list[dict[str, Any]] | None = None if pairs is None else list(pairs)
        self._pairs_tsv = pairs_tsv
        self._pairs_json = pairs_json

    @property
    def pairs_document(self) -> dict[str, Any] | None:
        """The q_pairs document (role "q"), parsed; None without pairs."""
        if self._pairs_json is None:
            return None
        document: dict[str, Any] = json.loads(self._pairs_json)
        return document

    @property
    def pairs_diagnostics(self) -> dict[str, Any] | None:
        document = self.pairs_document
        return None if document is None else document["diagnostics"]

    def write_pairs(self, path: PathLike, kind: str | None = None) -> None:
        """Write the pairs as the CLI's --pairs-out does: q_pairs JSON when
        ``kind`` is "json" or the path ends in .json, TSV otherwise."""
        if self._pairs_tsv is None or self._pairs_json is None:
            raise ValueError("this result has no pairs; detect with pairs=True")
        if kind is None:
            kind = "json" if str(path).endswith(".json") else "tsv"
        if kind not in ("tsv", "json"):
            raise ValueError(f"kind must be 'tsv' or 'json', not {kind!r}")
        text = self._pairs_json if kind == "json" else self._pairs_tsv
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)

    def _resolved_threshold(self) -> float | None:
        for row in self.rows:
            value = row.get("Cognator_Resolved_Threshold")
            if value is None or value == "":
                continue
            return float(value)
        return None

    def __len__(self) -> int:
        return len(self.rows)

    def __iter__(self) -> Iterator[Record]:
        return iter(self.rows)

    def __getitem__(self, index: int) -> Record:
        return self.rows[index]

    def clusters(self) -> dict[str, list[str]]:
        clusters: dict[str, list[str]] = {}
        for row in self.rows:
            clusters.setdefault(row["Cognator_ID"], []).append(row["ID"])
        return clusters

    def by_id(self) -> dict[str, Record]:
        return {row["ID"]: row for row in self.rows}

    def to_records(self) -> list[dict[str, Any]]:
        return [dict(row) for row in self.rows]

    def write_sidecar(self, path: PathLike) -> None:
        columns: tuple[str, ...] = SIDECAR_COLUMNS
        if self.rows and MORPHEME_IDS_COLUMN in self.rows[0]:
            columns = (*SIDECAR_COLUMNS, MORPHEME_IDS_COLUMN)
        with open(path, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(
                handle, fieldnames=columns, delimiter="\t", lineterminator="\n"
            )
            writer.writeheader()
            for row in self.rows:
                writer.writerow({name: row.get(name, "") for name in columns})

    def evaluate(
        self, truth: Wordlist | PathLike | TableLike, **options: Any
    ) -> EvaluationResult:
        return evaluate(self, truth, **options)


class Wordlist:
    def __init__(self, records: Sequence[Record]) -> None:
        self.records: list[Record] = list(records)

    @classmethod
    def from_file(cls, path: PathLike) -> Wordlist:
        return cls(_records_from_path(path))

    @classmethod
    def from_records(cls, records: TableLike) -> Wordlist:
        return cls(_records_from_table_like(records))

    def __len__(self) -> int:
        return len(self.records)

    def __iter__(self) -> Iterator[Record]:
        return iter(self.records)

    def detect(self, **options: Any) -> CognateResult:
        return detect_records(self.records, **options)

    def evaluate(
        self, pred: CognateResult | PathLike | TableLike, **options: Any
    ) -> EvaluationResult:
        return evaluate(pred, self, **options)

    def concepts(self) -> list[str]:
        return sorted({record.get("Concept", "") for record in self.records})

    def languages(self) -> list[str]:
        return sorted({record.get("Language_ID", "") for record in self.records})
