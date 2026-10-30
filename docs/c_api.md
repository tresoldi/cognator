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

## Pairwise Export

Added without changing any existing struct, enum or signature, so
`CG_ABI_VERSION` stays 1: `cg_options`, `cg_sidecar_row` and the opaque
`cg_result` keep their layout for every existing caller (the result's new
fields are internal).

- `cg_pairs_spec` (`unit`, `null_samples`, `min_null`, `null_seed`,
  `morpheme_clusters`) is filled by `cg_pairs_spec_init_defaults`;
  `CG_PAIRS_WORDS` / `CG_PAIRS_MORPHEMES` select the unit. Zero or negative
  sizes and a zero seed mean the documented defaults.
- `cg_detect_with_pairs(ctx, records, options, spec, &result)` is `cg_detect`
  plus the export; its sidecar rows equal `cg_detect`'s for the same options.
  `spec` NULL or `CG_PAIRS_NONE` is `cg_detect`; any other unit value is
  `CG_ERR_UNSUPPORTED_OPTION`.
- `cg_result_pair_count` / `cg_result_pair_at` return borrowed
  `cg_pair_row`s, valid until `cg_result_free`. `cg_result_pairs_copy`
  deep-copies them; release the copy with `cg_pair_rows_free(rows, count)`.
- `cg_result_pairs_summary` fills a `cg_pairs_summary`; it returns
  `CG_ERR_INVALID_ARGUMENT` for a result made without pairs.
- `cg_result_morpheme_ids_at(result, row)` is the borrowed
  `Cognator_Morpheme_IDs` value of a sidecar row, or NULL when morpheme
  clusters were not computed.
- `cg_result_pairs_tsv` and `cg_result_pairs_json` return owned strings,
  released with `cg_string_free`. `cg_write_pairs(path, result, json)` and
  `cg_write_sidecar_morphemes(path, result)` write files.

What the weights mean, and do not, is in `docs/pairs.md`.

## Python Binding Guidance

The Python wrapper should depend only on `include/cognator.h` symbols and should
not include files from `src/internal`. It should copy result rows before freeing
`cg_result`, use `cg_string_free` for owned strings, and check
`cg_abi_version()` at import time.
