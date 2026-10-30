# Python Usage

The Python package is the intended user-facing surface once the Go code is
removed. The direct path is intentionally small:

```sh
python -m pip install .
cognator detect --in forms.tsv --out sidecar.tsv
```

For Python code:

```python
import cognator

wordlist = cognator.load("forms.tsv")
result = wordlist.detect(preset="support-adaptive-bridge")

print(result.clusters())
print(result.resolved_threshold)
result.write_sidecar("sidecar.tsv")
```

For scripts, `detect()` also accepts a path, a list of records, or a pandas-like
object with `to_dict("records")`:

```python
rows = cognator.detect([
    {"ID": "a1", "Language_ID": "A", "Concept": "HAND", "Segments": "p a"},
    {"ID": "b1", "Language_ID": "B", "Concept": "HAND", "Segments": "p a"},
])
```

The return value is a `CognateResult`, which is iterable, indexable, convertible
to records, and can be grouped with `clusters()`.

The default package has no runtime Python dependencies; pandas-style inputs are
accepted by protocol when an object exposes `to_dict("records")`, but pandas is
not required.

## Stable API

The supported Python API is intentionally small:

- `cognator.load(path)` returns a `Wordlist`.
- `cognator.detect(data, **options)` accepts a path, records, or a dataframe-like object.
- `cognator.detect_records(records, **options)` is the explicit records-only form.
- `cognator.evaluate(pred, truth, per_concept=True)` returns B-cubed metrics.
- `CognateResult.write_sidecar(path)` writes the sidecar TSV.
- `CognateResult.evaluate(truth)` and `Wordlist.evaluate(pred)` are convenience forms.

Evaluation uses the same within-concept B-cubed target as the C CLI:

```python
wordlist = cognator.load("forms.tsv")
result = wordlist.detect(preset="support-adaptive-bridge")
metrics = result.evaluate(wordlist)

print(metrics.precision, metrics.recall, metrics.f1)
print(metrics.to_dict())
```

## Pairwise export

`detect` also returns the weighted pairs (`q(pairs)`) when asked; the weights
are background-match rates, not cognacy probabilities -- see `docs/pairs.md`.

```python
result = cognator.detect("forms.tsv", pairs="morphemes", morpheme_ids=True,
                         preset="support-adaptive-bridge")
result.pairs[0]          # {"form_a", "morph_a", "form_b", "morph_b", "weight",
                         #  "distance", "global", "local", "overlap",
                         #  "null_n", "null_scope"}
result.pairs_document    # the q_pairs JSON document, parsed
result.write_pairs("pairs.json")   # or .tsv; the same bytes the CLI writes
result.write_sidecar("sidecar.tsv")  # with Cognator_Morpheme_IDs

cognator.detect_pairs("forms.tsv")   # word pairs only, as a list
```

`pairs=True` or `"words"` gives word pairs, `"morphemes"` per-morpheme pairs
split at `+`; `null_samples`, `null_min` and `null_seed` set the null sample.
Without any of these keywords `detect` makes exactly the call it always made,
and `result.pairs` is `None`.

The raw C extension module is private; user code should import from `cognator`,
not `cognator._cognator`.
