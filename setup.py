from pathlib import Path
import os
import sys

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
            define_macros=[("_POSIX_C_SOURCE", "200809L")],
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
