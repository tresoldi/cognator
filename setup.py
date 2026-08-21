import os
import re
import subprocess
import sys
from pathlib import Path

from setuptools import Extension, setup

ROOT = Path(__file__).parent.resolve()
# The pinned copy under third_party/, the same one CMakeLists.txt builds. The
# environment variable is the same override CMake offers, and carries the same
# warning: cognator's thresholds are calibrated against this merkmal's
# segment-distance scale. See third_party/merkmal/README.md.
MERKMAL = Path(
    os.environ.get("COGNATOR_MERKMAL_SOURCE_DIR", ROOT / "third_party" / "merkmal")
).resolve()

if not (MERKMAL / "include" / "merkmal.h").exists():
    raise SystemExit(
        "merkmal C source checkout not found; set COGNATOR_MERKMAL_SOURCE_DIR"
    )


def _merkmal_provenance():
    """Which merkmal is being compiled in, as (version, commit).

    Compiled into the library and reported with every result, because
    cognator's thresholds are values on merkmal's segment-distance scale and
    that scale moves with its feature inventory. src/context.c refuses to
    compile without these rather than accept a default: an empty provenance
    string is indistinguishable from a correct one at the point it matters.

    The pinned copy carries a PINNED file. An external checkout does not, so
    fall back to its own project() declaration -- the same one CMake reads --
    and its git revision.
    """
    pinned = MERKMAL / "PINNED"
    if pinned.exists():
        fields = {}
        for line in pinned.read_text(encoding="utf-8").splitlines():
            key, _, value = line.partition(" ")
            fields[key] = value.strip()
        version = fields.get("version", "")
        commit = fields.get("commit", "")[:12]
        if version and commit:
            return version, commit
        raise SystemExit(f"{pinned} names no version and commit")

    version = ""
    cmakelists = MERKMAL / "CMakeLists.txt"
    if cmakelists.exists():
        match = re.search(
            r"project\s*\(\s*merkmal.*?VERSION\s+([0-9][^\s)]*)",
            cmakelists.read_text(encoding="utf-8"),
            re.S,
        )
        if match:
            version = match.group(1)
    try:
        commit = subprocess.run(
            ["git", "-C", str(MERKMAL), "rev-parse", "--short=12", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        commit = "unknown"
    if not version:
        raise SystemExit(
            f"cannot determine the merkmal version at {MERKMAL}; it is "
            "compiled in and reported with every result"
        )
    return version, commit


MERKMAL_VERSION, MERKMAL_COMMIT = _merkmal_provenance()

sources = [
    ROOT / "python" / "src" / "cognator_module.c",
    ROOT / "src" / "context.c",
    ROOT / "src" / "data.c",
    ROOT / "src" / "detect.c",
    ROOT / "src" / "eval.c",
    ROOT / "src" / "align.c",
    ROOT / "src" / "graph.c",
    ROOT / "src" / "oov.c",
    ROOT / "src" / "options.c",
    ROOT / "src" / "posterior.c",
    ROOT / "src" / "segment.c",
    ROOT / "src" / "wordlist.c",
    MERKMAL / "src" / "feature_set.c",
    MERKMAL / "src" / "geometry.c",
    MERKMAL / "src" / "registry.c",
    MERKMAL / "src" / "status.c",
    MERKMAL / "src" / "string_list.c",
    MERKMAL / "src" / "system.c",
    MERKMAL / "src" / "unicode.c",
    MERKMAL / "src" / "generated" / "builtin_data.c",
]


def source_path(path):
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path)

extra_compile_args = []
if os.name != "nt":
    extra_compile_args.extend(["-std=c99", "-Wall", "-Wextra", "-Wpedantic"])

libraries = []
if sys.platform.startswith(("linux", "freebsd")):
    libraries.append("m")

setup(
    ext_modules=[
        Extension(
            "cognator._cognator",
            sources=[source_path(path) for path in sources],
            define_macros=[
                ("_POSIX_C_SOURCE", "200809L"),
                ("COGNATOR_MERKMAL_VERSION", f'"{MERKMAL_VERSION}"'),
                ("COGNATOR_MERKMAL_COMMIT", f'"{MERKMAL_COMMIT}"'),
            ],
            include_dirs=[
                str(ROOT / "include"),
                str(ROOT / "src"),
                str(MERKMAL / "include"),
                str(MERKMAL / "src"),
            ],
            libraries=libraries,
            extra_compile_args=extra_compile_args,
        )
    ]
)
