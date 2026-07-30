# Go Retirement Cutover

Milestone 16 froze the Go implementation as archival reference code; milestone
10 removes it from the normal development and user workflow.

The active implementation is now the C library, C CLI, and Python wrapper. Go
remains in the repository only for historical comparison, parity
investigations, and final physical removal planning.

## Policy

- Do not add new modeling behavior to Go.
- Do not add new user-facing Go commands or flags.
- Do not update normal benchmark scripts to depend on Go.
- Only fix Go when needed to read old behavior for parity or to keep archival
  tests understandable.
- Implement new functionality in the C library, C CLI, or Python wrapper.
- Scripts that still invoke Go must require an explicit `--allow-archival-go`
  opt-in or be moved under an archival path.

## User Surface

`go run ./cmd/cognator detect` and `go run ./cmd/cognator eval` now print an
archive warning before running. The warning is intentional: these commands are
not the supported product path.

Use the Python package or C CLI instead:

```sh
python -m pip install .
cognator detect --in forms.csv --out sidecar.tsv
python - <<'PY'
import cognator
wordlist = cognator.load("forms.csv")
result = wordlist.detect()
print(result.evaluate(wordlist).f1)
PY
```

```sh
cmake -S . -B build -DCOGNATOR_BUILD_CLI=ON
cmake --build build -j2
./build/cognator detect --in forms.csv --out sidecar.tsv
./build/cognator eval --pred sidecar.tsv --truth forms.csv
./build/cognator sweep --in forms.csv
./build/cognator bench --in forms.csv --out-tsv benchmark.tsv
./build/cognator diag --in forms.csv
./build/cognator oovscan --in forms.csv
```

## Removal Gate

The Python wrapper now covers normal file and in-memory workflows, sidecar
writing, and B-cubed evaluation. The C/Python benchmark path is documented in
`benchmark/milestone24/` and `scripts/run_milestone24_benchmark.py`.

Remaining blockers before physical deletion:

- Port or replace the old eval-table benchmark harness (`cmd/bench`,
  `cmd/benchcompare`, and fresh-mode arcaverborum report scripts) in C/Python.
- Build and publish generated C/WASM browser artifacts from `web/build-wasm.sh`
  in an Emscripten environment.
- Remove archival Go parity tests once no open parity question needs executable
  Go reference behavior.
