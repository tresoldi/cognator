from pathlib import Path
import csv
from collections import defaultdict
from dataclasses import dataclass
import tempfile

from ._cognator import detect_file


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

    def to_dict(self):
        return {
            "scored_count": self.scored_count,
            "pred_count": self.pred_count,
            "truth_count": self.truth_count,
            "precision": self.precision,
            "recall": self.recall,
            "f1": self.f1,
        }


def _records_from_table_like(data):
    if hasattr(data, "to_dict"):
        try:
            records = data.to_dict("records")
        except TypeError:
            records = data.to_dict(orient="records")
        return list(records)
    return list(data)


def _record_value(record, name):
    if hasattr(record, "get"):
        value = record.get(name, "")
    else:
        value = getattr(record, name, "")
    return "" if value is None else str(value)


def _records_from_path(path):
    delimiter = "\t" if str(path).endswith(".tsv") else ","
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter=delimiter))


def _records_from_input(data):
    if isinstance(data, Wordlist):
        return data.records
    if isinstance(data, CognateResult):
        return data.rows
    if isinstance(data, (str, bytes, Path)):
        return _records_from_path(data)
    return _records_from_table_like(data)


def _detect_records(records, options):
    rows = _records_from_table_like(records)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="", suffix=".tsv") as handle:
        writer = csv.DictWriter(handle, fieldnames=INPUT_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for record in rows:
            writer.writerow({name: _record_value(record, name) for name in INPUT_COLUMNS})
        handle.flush()
        return detect_file(handle.name, **options)


def detect(data, **options):
    if isinstance(data, (str, bytes, Path)):
        return CognateResult(detect_file(str(data), **options))
    return CognateResult(_detect_records(data, options))


def detect_records(records, **options):
    return CognateResult(_detect_records(records, options))


def load(path):
    return Wordlist.from_file(path)


def evaluate(pred, truth, *, truth_column="Cognacy", per_concept=True):
    pred_rows = _records_from_input(pred)
    truth_rows = _records_from_input(truth)
    pred_assignments = {}
    truth_assignments = {}

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


def _bcubed(pred, truth):
    pred_members = defaultdict(list)
    truth_members = defaultdict(list)
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
    def __init__(self, rows):
        self.rows = list(rows)

    def __len__(self):
        return len(self.rows)

    def __iter__(self):
        return iter(self.rows)

    def __getitem__(self, index):
        return self.rows[index]

    def clusters(self):
        clusters = {}
        for row in self.rows:
            clusters.setdefault(row["Cognator_ID"], []).append(row["ID"])
        return clusters

    def by_id(self):
        return {row["ID"]: row for row in self.rows}

    def to_records(self):
        return [dict(row) for row in self.rows]

    def write_sidecar(self, path):
        with open(path, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=SIDECAR_COLUMNS, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            for row in self.rows:
                writer.writerow({name: row.get(name, "") for name in SIDECAR_COLUMNS})

    def evaluate(self, truth, **options):
        return evaluate(self, truth, **options)


class Wordlist:
    def __init__(self, records):
        self.records = list(records)

    @classmethod
    def from_file(cls, path):
        return cls(_records_from_path(path))

    @classmethod
    def from_records(cls, records):
        return cls(_records_from_table_like(records))

    def __len__(self):
        return len(self.records)

    def __iter__(self):
        return iter(self.records)

    def detect(self, **options):
        return detect_records(self.records, **options)

    def evaluate(self, pred, **options):
        return evaluate(pred, self, **options)

    def concepts(self):
        return sorted({record.get("Concept", "") for record in self.records})

    def languages(self):
        return sorted({record.get("Language_ID", "") for record in self.records})
