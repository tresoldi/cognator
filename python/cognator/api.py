from __future__ import annotations

from pathlib import Path
import csv
from collections import defaultdict
from dataclasses import dataclass
import tempfile
from typing import Any, Dict, Iterator, List, Mapping, Optional, Sequence, Union

from ._cognator import detect_file

# A row, as it comes from csv.DictReader, from the extension, or from a
# DataFrame's to_dict("records"). Values are read back as strings by
# _record_value, but the extension returns floats and ints, so the value type
# is Any rather than a lie.
Record = Mapping[str, Any]

# Anything _records_from_table_like accepts: a DataFrame or other object with a
# to_dict method, or any iterable of records. Deliberately Any -- narrowing it
# would name pandas, which is not a dependency and must not become one.
TableLike = Any

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


@dataclass(frozen=True)
class EvaluationResult:
    scored_count: int
    pred_count: int
    truth_count: int
    precision: float
    recall: float
    f1: float

    def to_dict(self) -> Dict[str, Any]:
        return {
            "scored_count": self.scored_count,
            "pred_count": self.pred_count,
            "truth_count": self.truth_count,
            "precision": self.precision,
            "recall": self.recall,
            "f1": self.f1,
        }


def _records_from_table_like(data: TableLike) -> List[Record]:
    if hasattr(data, "to_dict"):
        try:
            records = data.to_dict("records")
        except TypeError:
            records = data.to_dict(orient="records")
        return list(records)
    return list(data)


def _record_value(record: Any, name: str) -> str:
    if hasattr(record, "get"):
        value = record.get(name, "")
    else:
        value = getattr(record, name, "")
    return "" if value is None else str(value)


def _records_from_path(path: PathLike) -> List[Record]:
    delimiter = "\t" if str(path).endswith(".tsv") else ","
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter=delimiter))


def _records_from_input(data: Union["Wordlist", "CognateResult", PathLike, TableLike]) -> List[Record]:
    if isinstance(data, Wordlist):
        return data.records
    if isinstance(data, CognateResult):
        return data.rows
    if isinstance(data, (str, bytes, Path)):
        return _records_from_path(data)
    return _records_from_table_like(data)


def _detect_records(records: TableLike, options: Mapping[str, Any]) -> List[Dict[str, Any]]:
    rows = _records_from_table_like(records)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="", suffix=".tsv") as handle:
        writer = csv.DictWriter(handle, fieldnames=INPUT_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for record in rows:
            writer.writerow({name: _record_value(record, name) for name in INPUT_COLUMNS})
        handle.flush()
        return detect_file(handle.name, **options)


def detect(data: Union[PathLike, TableLike], **options: Any) -> "CognateResult":
    if isinstance(data, (str, bytes, Path)):
        return CognateResult(detect_file(str(data), **options))
    return CognateResult(_detect_records(data, options))


def detect_records(records: TableLike, **options: Any) -> "CognateResult":
    return CognateResult(_detect_records(records, options))


def load(path: PathLike) -> "Wordlist":
    return Wordlist.from_file(path)


def evaluate(
    pred: Union["Wordlist", "CognateResult", PathLike, TableLike],
    truth: Union["Wordlist", "CognateResult", PathLike, TableLike],
    *,
    truth_column: str = "Cognacy",
    per_concept: bool = True,
) -> EvaluationResult:
    pred_rows = _records_from_input(pred)
    truth_rows = _records_from_input(truth)
    pred_assignments: Dict[str, str] = {}
    truth_assignments: Dict[str, str] = {}

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
    pred_members: Dict[str, List[str]] = defaultdict(list)
    truth_members: Dict[str, List[str]] = defaultdict(list)
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
    def __init__(self, rows: Sequence[Record]) -> None:
        self.rows: List[Record] = list(rows)
        self.resolved_threshold = self._resolved_threshold()

    def _resolved_threshold(self) -> Optional[float]:
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

    def clusters(self) -> Dict[str, List[str]]:
        clusters: Dict[str, List[str]] = {}
        for row in self.rows:
            clusters.setdefault(row["Cognator_ID"], []).append(row["ID"])
        return clusters

    def by_id(self) -> Dict[str, Record]:
        return {row["ID"]: row for row in self.rows}

    def to_records(self) -> List[Dict[str, Any]]:
        return [dict(row) for row in self.rows]

    def write_sidecar(self, path: PathLike) -> None:
        with open(path, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=SIDECAR_COLUMNS, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            for row in self.rows:
                writer.writerow({name: row.get(name, "") for name in SIDECAR_COLUMNS})

    def evaluate(self, truth: Union["Wordlist", PathLike, TableLike], **options: Any) -> EvaluationResult:
        return evaluate(self, truth, **options)


class Wordlist:
    def __init__(self, records: Sequence[Record]) -> None:
        self.records: List[Record] = list(records)

    @classmethod
    def from_file(cls, path: PathLike) -> "Wordlist":
        return cls(_records_from_path(path))

    @classmethod
    def from_records(cls, records: TableLike) -> "Wordlist":
        return cls(_records_from_table_like(records))

    def __len__(self) -> int:
        return len(self.records)

    def __iter__(self) -> Iterator[Record]:
        return iter(self.records)

    def detect(self, **options: Any) -> "CognateResult":
        return detect_records(self.records, **options)

    def evaluate(self, pred: Union["CognateResult", PathLike, TableLike], **options: Any) -> EvaluationResult:
        return evaluate(pred, self, **options)

    def concepts(self) -> List[str]:
        return sorted({record.get("Concept", "") for record in self.records})

    def languages(self) -> List[str]:
        return sorted({record.get("Language_ID", "") for record in self.records})
