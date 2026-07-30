from pathlib import Path
import csv
import tempfile

from ._cognator import Error, abi_version, detect_file, version


_INPUT_COLUMNS = ("ID", "Language_ID", "Concept", "Segments", "Family", "Cognacy")


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
        writer = csv.DictWriter(handle, fieldnames=_INPUT_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for record in rows:
            writer.writerow({name: _record_value(record, name) for name in _INPUT_COLUMNS})
        handle.flush()
        return detect_file(handle.name, **options)


def detect(data, **options):
    if isinstance(data, (str, bytes, Path)):
        return detect_file(str(data), **options)
    return _detect_records(data, options)


def detect_records(records, **options):
    return _detect_records(records, options)


__all__ = ["Error", "abi_version", "detect", "detect_file", "detect_records", "version"]
