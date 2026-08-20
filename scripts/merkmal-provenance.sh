#!/usr/bin/env bash
#
# Checks that third_party/merkmal is still the snapshot PINNED says it is.
#
#   ./scripts/merkmal-provenance.sh --check    # asserts the tree is unmodified
#   ./scripts/merkmal-provenance.sh --write    # restamp, after moving the pin
#
# The pinned merkmal is committed source, which means it is editable, and a
# one-line local fix to a vendored dependency is the kind of change that
# survives for a year without anyone knowing it is there. Every benchmark
# number in this repository was measured against upstream 3d21e08 exactly; a
# patched copy invalidates them just as surely as a different release would,
# and much more quietly.
#
# Hashes the files rather than asking git whether they are modified, so it also
# catches a snapshot committed from something other than the named upstream
# commit -- which is the mistake --write is most likely to be used to make.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
vendor_dir="$repo_dir/third_party/merkmal"
stamp="$vendor_dir/PINNED"

# The sources and the header only. LICENSE, README.md, PINNED and sources.cmake
# are this repository's files about the snapshot, not part of it, and stamping
# them would make PINNED's own hash line self-referential.
tree_hash() {
    cd "$vendor_dir"
    find include src -type f \( -name '*.c' -o -name '*.h' \) -print0 \
        | LC_ALL=C sort -z \
        | while IFS= read -r -d '' path; do
              # The name goes in too, so a rename is a change.
              printf '%s  %s\n' "$(sha256sum < "$path" | cut -d' ' -f1)" "$path"
          done \
        | sha256sum | cut -d' ' -f1
}

mode="${1:---check}"
current="$(tree_hash)"

case "$mode" in
--write)
    commit="$(awk '$1 == "commit" { print $2 }' "$stamp")"
    if [ -z "$commit" ]; then
        echo "merkmal-provenance: PINNED names no commit; edit it first" >&2
        exit 1
    fi
    grep -v '^tree_sha256 ' "$stamp" > "$stamp.tmp"
    printf 'tree_sha256 %s\n' "$current" >> "$stamp.tmp"
    mv "$stamp.tmp" "$stamp"
    echo "merkmal-provenance: recorded ${current:0:12} for ${commit:0:12}"
    ;;
--check)
    recorded="$(awk '$1 == "tree_sha256" { print $2 }' "$stamp")"
    if [ -z "$recorded" ]; then
        echo "merkmal-provenance: PINNED has no tree_sha256; run --write" >&2
        exit 1
    fi
    if [ "$recorded" != "$current" ]; then
        cat >&2 <<EOF
merkmal-provenance: third_party/merkmal is not the snapshot PINNED describes.

  recorded  $recorded
  tree      $current

If you edited the vendored sources, revert them: patch upstream instead. If you
moved the pin, update PINNED's commit line and rerun with --write -- and read
docs/merkmal_pin.md first, because moving it is a recalibration.
EOF
        exit 1
    fi
    echo "merkmal-provenance: current (${current:0:12})"
    ;;
*)
    echo "usage: $0 [--check|--write]" >&2
    exit 2
    ;;
esac
