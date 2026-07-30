# Go Freeze

Milestone 16 freezes the Go implementation as archival reference code.

The active implementation is now the C library and C CLI. Go remains in the
repository only for historical comparison, parity investigations, and final
removal planning.

## Policy

- Do not add new modeling behavior to Go.
- Do not add new user-facing Go commands or flags.
- Do not update benchmark scripts to depend on Go.
- Only fix Go when needed to read old behavior for parity or to keep archival
  tests understandable.
- Implement new functionality in the C library, C CLI, or the future Python
  wrapper.

## User Surface

`go run ./cmd/cognator detect` and `go run ./cmd/cognator eval` now print an
archive warning before running. The warning is intentional: these commands are
not the supported product path.

Use the C CLI instead:

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

Go can be removed after the Python wrapper covers the normal file and in-memory
workflows, the C/Python benchmark path is documented, and no unresolved parity
question requires the Go code as executable reference.
