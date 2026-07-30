# Python Installability

The Python package is the intended user-facing install path. A normal source
install builds the C extension in place, compiles Cognator's C sources, and uses
the vendored merkmal C runtime subset under `python/vendor/merkmal`; users do
not need Go, CMake, LingPy, or a separate merkmal checkout for the default path.

## Local Install

```sh
python -m pip install .
```

Smoke check:

```sh
python -c "import cognator; print(cognator.version())"
cognator --help
```

## Command Line

The package installs the natural `cognator` command and keeps `cognator-py` as a
compatibility alias for earlier wrapper milestones:

```sh
cognator detect --in forms.tsv --out sidecar.tsv
```

The module form is equivalent and does not require the console script directory
to be on `PATH`:

```sh
python -m cognator detect --in forms.tsv --out sidecar.tsv
```

## Wheels

```sh
python -m pip wheel . -w dist
python -m pip install dist/cognator-*.whl
```

`pyproject.toml` includes initial `cibuildwheel` settings for CPython 3.9
through 3.14; release automation still needs platform CI, but the source tree is
self-contained for local wheel builds.

## Merkmal Development Override

Use `COGNATOR_MERKMAL_SOURCE_DIR` only while coordinating changes with the
merkmal C rework branch:

```sh
COGNATOR_MERKMAL_SOURCE_DIR=../merkmal python -m pip install .
```
