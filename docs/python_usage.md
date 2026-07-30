# Python Usage

The Python package is the intended user-facing surface once the Go code is
removed. The direct path is:

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
