from pathlib import Path
import csv
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


class Wordlist:
    def __init__(self, records):
        self.records = list(records)

    @classmethod
    def from_file(cls, path):
        delimiter = "\t" if str(path).endswith(".tsv") else ","
        with open(path, newline="", encoding="utf-8") as handle:
            return cls(csv.DictReader(handle, delimiter=delimiter))

    @classmethod
    def from_records(cls, records):
        return cls(_records_from_table_like(records))

    def __len__(self):
        return len(self.records)

    def __iter__(self):
        return iter(self.records)

    def detect(self, **options):
        return detect_records(self.records, **options)

    def concepts(self):
        return sorted({record.get("Concept", "") for record in self.records})

    def languages(self):
        return sorted({record.get("Language_ID", "") for record in self.records})
