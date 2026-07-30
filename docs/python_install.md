# Python Installability

Milestone 21 makes the Python package self-contained for local builds: the
extension compiles Cognator's C sources and the small merkmal C runtime subset
vendored under `python/vendor/merkmal`.

Normal local install:

```sh
python -m pip install .
```

Build a wheel:

```sh
python -m pip wheel . -w dist
```

Override the vendored merkmal source, useful during coordinated C API work:

```sh
COGNATOR_MERKMAL_SOURCE_DIR=../merkmal python -m pip install .
```

The package exposes a console entry point:

```sh
cognator-py detect --in forms.tsv --out sidecar.tsv
```

`pyproject.toml` includes initial `cibuildwheel` settings for future manylinux,
macOS, and Windows wheel automation.
