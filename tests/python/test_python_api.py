import csv
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import cognator


ROOT = Path(__file__).resolve().parents[2]
LATIN = ROOT / "tests" / "fixtures" / "linguistic" / "latin_romance_triplets.tsv"
EXTENDED = ROOT / "tests" / "fixtures" / "linguistic" / "latin_romance_extended.tsv"


def cluster_pairs(rows):
    by_id = {row["ID"]: row["Cognator_ID"] for row in rows}
    ids = sorted(by_id)
    return {
        (left, right)
        for index, left in enumerate(ids)
        for right in ids[index + 1 :]
        if by_id[left] == by_id[right]
    }


def read_sidecar(path):
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


class FrameLike:
    def __init__(self, rows):
        self.rows = rows

    def to_dict(self, orient="records"):
        if orient != "records":
            raise AssertionError(orient)
        return list(self.rows)


class PythonApiTests(unittest.TestCase):
    def test_import_version_and_abi(self):
        self.assertEqual(cognator.version(), "0.1.0")
        self.assertEqual(cognator.abi_version(), 1)

    def test_detect_from_path(self):
        result = cognator.detect(
            LATIN,
            preset="support-adaptive-bridge",
            threshold=0.70,
            bootstrap=0,
            scorer="posterior",
            min_pairs=1,
            seed=17,
        )
        self.assertEqual(len(result), 12)
        self.assertIn("romance:NIGHT:0", result.clusters())

    def test_detect_from_records_and_dataframe_like(self):
        records = [
            {"ID": "a1", "Language_ID": "A", "Concept": "HAND", "Segments": "p a", "Family": "fam", "Cognacy": "1"},
            {"ID": "b1", "Language_ID": "B", "Concept": "HAND", "Segments": "p a", "Family": "fam", "Cognacy": "1"},
        ]
        from_records = cognator.detect_records(records, threshold=0.70, min_pairs=1)
        from_frame = cognator.detect(FrameLike(records), threshold=0.70, min_pairs=1)
        self.assertEqual(len(from_records), 2)
        self.assertEqual(cluster_pairs(from_records), cluster_pairs(from_frame))

    def test_wordlist_workflow_and_sidecar_export(self):
        wordlist = cognator.load(LATIN)
        self.assertEqual(len(wordlist), 12)
        self.assertIn("NIGHT", wordlist.concepts())
        result = wordlist.detect(
            preset="support-adaptive-bridge",
            threshold=0.70,
            bootstrap=0,
            scorer="posterior",
            min_pairs=1,
            seed=17,
        )
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "sidecar.tsv"
            result.write_sidecar(out)
            rows = read_sidecar(out)
        self.assertEqual(len(rows), 12)
        self.assertEqual(rows[0]["ID"], result[0]["ID"])

    def test_unicode_fixture_and_repeated_calls(self):
        first = None
        for _ in range(3):
            result = cognator.detect(
                EXTENDED,
                preset="support-adaptive-bridge",
                threshold=0.70,
                bootstrap=0,
                scorer="posterior",
                min_pairs=1,
                seed=31,
            )
            pairs = cluster_pairs(result)
            if first is None:
                first = pairs
            self.assertEqual(pairs, first)
        self.assertEqual(len(result), 24)

    def test_error_handling(self):
        with self.assertRaises(cognator.Error):
            cognator.detect("/no/such/file.tsv")

    def test_python_cli_matches_c_cli_clusters(self):
        c_cli = os.environ.get("COGNATOR_CLI")
        if not c_cli:
            self.skipTest("COGNATOR_CLI not set")
        with tempfile.TemporaryDirectory() as tmp:
            py_out = Path(tmp) / "py.tsv"
            c_out = Path(tmp) / "c.tsv"
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "cognator",
                    "detect",
                    "--in",
                    str(LATIN),
                    "--out",
                    str(py_out),
                    "--threshold",
                    "0.70",
                    "--seed",
                    "17",
                ],
                check=True,
            )
            subprocess.run(
                [
                    c_cli,
                    "detect",
                    "--in",
                    str(LATIN),
                    "--out",
                    str(c_out),
                    "--preset",
                    "support-adaptive-bridge",
                    "--threshold",
                    "0.70",
                    "--bootstrap",
                    "0",
                    "--min-pairs",
                    "1",
                    "--seed",
                    "17",
                ],
                check=True,
            )
            self.assertEqual(cluster_pairs(read_sidecar(py_out)), cluster_pairs(read_sidecar(c_out)))


if __name__ == "__main__":
    unittest.main()
