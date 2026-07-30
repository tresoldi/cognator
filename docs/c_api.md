# C API Contract

Milestone 17 stabilizes the C API surface that the Python wrapper should bind.

## Versioning

- `cg_version_string()`, `cg_version_major()`, `cg_version_minor()`, and
  `cg_version_patch()` report the package version.
- `cg_abi_version()` reports the public C ABI generation.
- Increment `CG_ABI_VERSION` when changing exported struct layout, enum values,
  function signatures, ownership rules, or binary compatibility expectations.

## Ownership

- Handles created through `*_new`, `cg_read_table`, `cg_detect`,
  `cg_records_filter_family`, and similar constructors are owned by the caller
  and must be released with the matching `*_free`.
- Accessors named `*_get`, `*_at`, or `*_row_at` return borrowed pointers unless
  their documentation says otherwise.
- Borrowed pointers remain valid only until the owning handle is freed.
- Owned `char *` outputs are released with `cg_string_free`.
- `cg_sidecar_row_copy` and `cg_oov_row_copy` deep-copy borrowed result rows for
  wrappers that need values to outlive `cg_result`.
- Copied rows must be released with `cg_sidecar_row_clear` or
  `cg_oov_row_clear`.
- All `*_free` and `*_clear` functions accept `NULL` or zeroed structs.

## Error Handling

- Public functions return `cg_status` when failure is possible.
- `CG_OK` is success; every non-zero status is an error.
- `cg_status_string()` returns a static diagnostic string owned by the library.
- Functions that fill output pointers leave ownership with the caller only after
  returning `CG_OK`.

## Python Binding Guidance

The Python wrapper should depend only on `include/cognator.h` symbols and should
not include files from `src/internal`. It should copy result rows before freeing
`cg_result`, use `cg_string_free` for owned strings, and check
`cg_abi_version()` at import time.
