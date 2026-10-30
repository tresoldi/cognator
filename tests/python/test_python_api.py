import csv
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest

import cognator


ROOT = Path(__file__).resolve().parents[2]
LATIN = ROOT / "tests" / "fixtures" / "linguistic" / "latin_romance_triplets.tsv"
EXTENDED = ROOT / "tests" / "fixtures" / "linguistic" / "latin_romance_extended.tsv"
PARTIAL = ROOT / "tests" / "fixtures" / "linguistic" / "partial_cognacy_compounds.tsv"
PAIR_OPTIONS = {
    "preset": "support-adaptive-bridge",
    "threshold": 0.0,
    "min_pairs": 1,
    "scorer": "posterior",
    "seed": 3,
}


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
        self.assertIn("evaluate", cognator.__all__)
        self.assertNotIn("detect_file", cognator.__all__)
        self.assertFalse(hasattr(cognator, "detect_file"))

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
        self.assertAlmostEqual(result.resolved_threshold, 0.70)
        self.assertAlmostEqual(result[0]["Cognator_Resolved_Threshold"], 0.70)

    def test_detect_accepts_adaptive_threshold_options(self):
        result = cognator.detect(
            LATIN,
            preset="support-adaptive-bridge",
            threshold=0.0,
            threshold_mode="concept-shrink",
            threshold_min=0.64,
            threshold_max=0.74,
            threshold_shrink=0.75,
            bootstrap=0,
            scorer="posterior",
            min_pairs=1,
            seed=17,
        )
        self.assertEqual(len(result), 12)
        self.assertGreaterEqual(result.resolved_threshold, 0.64)
        self.assertLessEqual(result.resolved_threshold, 0.74)
        self.assertGreater(len(result.clusters()), 0)

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

    def test_evaluate_records(self):
        truth = [
            {"ID": "a", "Language_ID": "A", "Concept": "HAND", "Segments": "p a", "Cognacy": "1"},
            {"ID": "b", "Language_ID": "B", "Concept": "HAND", "Segments": "p a", "Cognacy": "1"},
            {"ID": "c", "Language_ID": "C", "Concept": "HAND", "Segments": "t a", "Cognacy": "2"},
            {"ID": "d", "Language_ID": "A", "Concept": "FOOT", "Segments": "p a", "Cognacy": "1"},
        ]
        pred = [
            {"ID": "a", "Cognator_ID": "x"},
            {"ID": "b", "Cognator_ID": "x"},
            {"ID": "c", "Cognator_ID": "y"},
            {"ID": "d", "Cognator_ID": "x"},
        ]
        result = cognator.evaluate(pred, truth)
        self.assertEqual(result.scored_count, 4)
        self.assertEqual(result.pred_count, 4)
        self.assertEqual(result.truth_count, 4)
        self.assertAlmostEqual(result.precision, 2 / 3)
        self.assertAlmostEqual(result.recall, 1.0)
        self.assertAlmostEqual(result.f1, 0.8)
        self.assertAlmostEqual(cognator.evaluate(pred, truth, per_concept=False).f1, 1.0)

    def test_evaluate_result_against_wordlist(self):
        wordlist = cognator.load(LATIN)
        result = wordlist.detect(
            preset="support-adaptive-bridge",
            threshold=0.70,
            bootstrap=0,
            scorer="posterior",
            min_pairs=1,
            seed=17,
        )
        metrics = result.evaluate(wordlist)
        self.assertIsInstance(metrics, cognator.EvaluationResult)
        self.assertEqual(metrics.scored_count, 12)
        self.assertGreater(metrics.f1, 0.8)
        self.assertEqual(metrics.to_dict()["f1"], metrics.f1)

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

    def test_packaging_console_entry_points(self):
        data = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
        scripts = data["project"]["scripts"]
        self.assertEqual(scripts["cognator"], "cognator.cli:main")
        self.assertEqual(scripts["cognator-py"], "cognator.cli:main")

    def test_module_cli_help_uses_installed_command_name(self):
        completed = subprocess.run(
            [sys.executable, "-m", "cognator", "--help"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        )
        self.assertIn("usage: cognator", completed.stdout)

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


class PairExportTests(unittest.TestCase):
    """detect(..., pairs=...): the q(pairs) export and partial cognacy."""

    @staticmethod
    def find(pairs, form_a, morph_a, form_b, morph_b):
        for pair in pairs:
            if (pair["form_a"], pair["morph_a"], pair["form_b"], pair["morph_b"]) == (
                form_a,
                morph_a,
                form_b,
                morph_b,
            ):
                return pair
        raise AssertionError(f"no pair {form_a}#{morph_a} ~ {form_b}#{morph_b}")

    def test_without_pairs_nothing_changes(self):
        plain = cognator.detect(PARTIAL, **PAIR_OPTIONS)
        self.assertIsNone(plain.pairs)
        self.assertIsNone(plain.pairs_document)
        with self.assertRaises(ValueError):
            plain.write_pairs("unused.tsv")
        words = cognator.detect(PARTIAL, pairs=True, **PAIR_OPTIONS)
        morphs = cognator.detect(PARTIAL, pairs="morphemes", morpheme_ids=True, **PAIR_OPTIONS)
        for result in (words, morphs):
            stripped = [
                {key: value for key, value in row.items() if key != "Cognator_Morpheme_IDs"}
                for row in result.rows
            ]
            self.assertEqual(stripped, plain.rows)

    def test_word_pairs(self):
        pairs = cognator.detect_pairs(PARTIAL, **PAIR_OPTIONS)
        self.assertEqual(len(pairs), 42)
        for pair in pairs:
            self.assertEqual((pair["morph_a"], pair["morph_b"]), (0, 0))
            self.assertGreaterEqual(pair["weight"], 0.0)
            self.assertLessEqual(pair["weight"], 1.0)

    def test_morpheme_pairs_and_clusters(self):
        result = cognator.detect(PARTIAL, pairs="morphemes", morpheme_ids=True, **PAIR_OPTIONS)
        pairs = result.pairs
        self.assertEqual(len(pairs), 150)
        self.assertFalse([pair for pair in pairs if pair["form_a"] == pair["form_b"]])
        self.assertGreaterEqual(self.find(pairs, "a_pebble", 0, "b_pebble", 0)["weight"], 0.9)
        self.assertLessEqual(self.find(pairs, "a_pebble", 1, "c_pebble", 1)["weight"], 0.1)
        suffix = self.find(pairs, "a_water", 1, "b_water", 1)
        root = self.find(pairs, "a_water", 0, "b_water", 0)
        self.assertAlmostEqual(suffix["distance"], root["distance"], places=12)
        self.assertLess(suffix["weight"], root["weight"])
        by_id = result.by_id()
        self.assertEqual(by_id["a_pebble"]["Cognator_Morpheme_IDs"], by_id["b_pebble"]["Cognator_Morpheme_IDs"])
        self.assertTrue(by_id["c_pebble"]["Cognator_Morpheme_IDs"].endswith(" -"))
        self.assertEqual(len(by_id["a_fire"]["Cognator_Morpheme_IDs"].split()), 1)

        document = result.pairs_document
        self.assertEqual(document["role"], "q")
        self.assertEqual(document["unit"], "morpheme")
        self.assertTrue(document["producer"].startswith("cognator@0.1.0+merkmal."))
        self.assertEqual(document["pairs"][0]["form_obs_a"], "a_pebble#0")
        self.assertIn("threshold", result.pairs_diagnostics)
        self.assertEqual(result.pairs_diagnostics["calibration"]["pairs"], 150)

        with tempfile.TemporaryDirectory() as tmp:
            sidecar = Path(tmp) / "sidecar.tsv"
            result.write_sidecar(sidecar)
            self.assertIn("Cognator_Morpheme_IDs", sidecar.read_text().splitlines()[0])
            tsv = Path(tmp) / "pairs.tsv"
            result.write_pairs(tsv)
            self.assertTrue(tsv.read_text().startswith("form_a\tmorph_a\tform_b\tmorph_b\tweight"))
            as_json = Path(tmp) / "pairs.json"
            result.write_pairs(as_json)
            self.assertEqual(json.loads(as_json.read_text())["role"], "q")

    def test_rejects_bad_pair_options(self):
        with self.assertRaises(ValueError):
            cognator.detect(PARTIAL, pairs="syllables", **PAIR_OPTIONS)
        with self.assertRaises(ValueError):
            cognator.detect(PARTIAL, pairs="words", morpheme_ids=True, **PAIR_OPTIONS)

    def test_pairs_match_the_c_cli_byte_for_byte(self):
        c_cli = os.environ.get("COGNATOR_CLI")
        if not c_cli:
            self.skipTest("COGNATOR_CLI not set")
        result = cognator.detect(PARTIAL, pairs="morphemes", **PAIR_OPTIONS)
        with tempfile.TemporaryDirectory() as tmp:
            for suffix in ("tsv", "json"):
                c_out = Path(tmp) / f"c.{suffix}"
                py_out = Path(tmp) / f"py.{suffix}"
                subprocess.run(
                    [
                        c_cli, "detect", "--in", str(PARTIAL), "--out", str(Path(tmp) / "sidecar.tsv"),
                        "--preset", "support-adaptive-bridge", "--threshold", "0", "--min-pairs", "1",
                        "--scorer", "posterior", "--seed", "3",
                        "--pairs-out", str(c_out), "--pairs-unit", "morphemes",
                    ],
                    check=True,
                    stderr=subprocess.DEVNULL,
                )
                result.write_pairs(py_out)
                self.assertEqual(py_out.read_bytes(), c_out.read_bytes())


class MerkmalProvenance(unittest.TestCase):
    """The wrapper reports the merkmal it was actually built against.

    setup.py compiles the macros in independently of CMake, so this is the only
    check that the pip path got them right -- and getting them wrong is silent:
    the same code that reads the provenance is the code that would report a
    wrong one. regulae shipped an empty string here for months.
    """

    def test_matches_the_pin(self):
        version, commit = cognator.merkmal()
        pinned = {}
        for line in (ROOT / "third_party" / "merkmal" / "PINNED").read_text().splitlines():
            key, _, value = line.partition(" ")
            pinned[key] = value.strip()

        self.assertEqual(version, pinned["version"])
        self.assertEqual(commit, pinned["commit"][:12])
        self.assertTrue(version and commit)

    def test_has_the_shape_a_consumer_records(self):
        version, commit = cognator.merkmal()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")
        self.assertRegex(commit, r"^[0-9a-f]{12}$")


if __name__ == "__main__":
    unittest.main()
