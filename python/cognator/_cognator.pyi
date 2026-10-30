"""Types for the compiled extension in python/src/cognator_module.c.

The package ships py.typed, so a checker follows imports into it and finds a
shared object it cannot read. Without this file the four names below are Any,
which quietly removes type checking from every call site in api.py -- the
opposite of what py.typed promises.

Keep in step with CognatorMethods and the module init in cognator_module.c.
"""

from typing import Any

class Error(Exception):
    """Raised for any non-OK cg_status returned by the C library."""

def detect_file(path: str, /, **options: Any) -> list[dict[str, Any]]:
    """Cluster a TSV/CSV wordlist, one dict of sidecar columns per input row.

    `options` are the cg_options fields; apply_options in the extension
    rejects an unknown name. They are typed loosely because that struct is
    where the detector's tunables live and they are documented there and in
    docs/python_usage.md, not duplicated here.
    """

def detect_file_pairs(path: str, /, **options: Any) -> dict[str, Any]:
    """detect_file plus the pairwise export.

    Besides the cg_options fields it reads `pairs_unit` ("words" or
    "morphemes"), `morpheme_ids`, `null_samples`, `null_min` and `null_seed`,
    and returns {"rows", "pairs", "tsv", "json"}: the sidecar rows, one dict
    per pair, and the TSV and q_pairs JSON the C writers produce.
    """

def version() -> str:
    """The linked C library's version string."""

def abi_version() -> int:
    """The linked C library's ABI version."""

def merkmal() -> tuple[str, str]:
    """(version, commit) of the merkmal compiled into the library.

    cognator's thresholds are values on merkmal's segment-distance scale, so
    two runs against different merkmals are not comparable. Record this beside
    any result kept longer than a session.
    """
