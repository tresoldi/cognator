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
result = wordlist.detect(preset="support-adaptive-bridge", threshold=0.70)

print(result.clusters())
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
result = wordlist.detect(threshold=0.68)
metrics = result.evaluate(wordlist)

print(metrics.precision, metrics.recall, metrics.f1)
print(metrics.to_dict())
```

The raw C extension module is private; user code should import from `cognator`,
not `cognator._cognator`.
