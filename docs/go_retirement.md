# Go Retirement

The Go implementation has been removed from the active tree.

The supported implementation is now:

- C library and C CLI in `src/`, `include/`, and `cmd/cognator-c/`
- Python package in `python/`
- C/Emscripten browser wrapper in `web/`
- C/Python benchmark and diagnostic scripts in `scripts/`

Historical benchmark artifacts remain under `benchmark/` where useful, but new
development must use the C/Python path. The old Go parity documents and tests
were removed with the Go source because there is no executable Go reference left
in this repository.
