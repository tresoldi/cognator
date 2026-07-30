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
