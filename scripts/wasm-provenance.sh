#!/usr/bin/env bash
#
# Records which sources produced the committed WebAssembly build, and checks
# that they still match.
#
#   ./scripts/wasm-provenance.sh --write    # web/build-wasm.sh calls this
#   ./scripts/wasm-provenance.sh --check    # asserts web/cognator.wasm is current
#
# web/cognator.wasm is committed rather than built on deploy, which means the
# page can ship an engine older than src/ without anything going red. The stamp
# in web/BUILD_INFO closes that: it hashes every source that goes into the
# artifact, so a source edit without a rebuild is a failing check rather than a
# silently stale deploy.
#
# Deliberately hashes sources rather than the artifact. emcc is not
# reproducible byte-for-byte across toolchain versions, so comparing a rebuilt
# artifact would fail for reasons that have nothing to do with staleness. It
# also keeps this runnable anywhere bash and git are -- no emsdk, no compiler --
# which is what lets the Pages workflow gate the deploy on it.
#
# third_party/merkmal is in the hash because it is compiled into the engine.
# Moving the pin changes what the page computes, and that has to be a rebuild.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
stamp="$repo_dir/web/BUILD_INFO"

# Tracked *and* untracked-but-not-ignored, so a new source file counts before
# it is committed; in a clean CI checkout the two sets are the same.
sources_hash() {
    cd "$repo_dir"
    git ls-files --cached --others --exclude-standard -z -- \
            src include third_party/merkmal web/cognator_wasm.c web/build-wasm.sh \
        | LC_ALL=C sort -z \
        | while IFS= read -r -d '' path; do
              # The name goes in the hash too, so a rename is a change.
              [ -f "$path" ] && printf '%s  %s\n' \
                  "$(sha256sum < "$path" | cut -d' ' -f1)" "$path"
          done \
        | sha256sum | cut -d' ' -f1
}

mode="${1:---check}"
current="$(sources_hash)"

case "$mode" in
--write)
    # A rebuild from unchanged sources produces an equivalent engine, so the
    # stamp does not move for it. Rewriting git_commit anyway would leave the
    # file dirty after every commit that touches nothing here, and check.sh's
    # staleness gate would fail on cosmetic churn -- which trains people to
    # commit the stamp without reading it, the exact habit the gate exists to
    # prevent. The commit recorded is therefore the one whose sources produced
    # this engine, which is the more useful answer anyway.
    if [ -f "$stamp" ] && \
       [ "$(awk '$1 == "sources_sha256" { print $2 }' "$stamp")" = "$current" ]; then
        echo "wasm-provenance: unchanged (${current:0:12})"
        exit 0
    fi
    commit="$(git -C "$repo_dir" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    {
        printf 'sources_sha256 %s\n' "$current"
        printf 'git_commit %s\n' "$commit"
        printf 'merkmal_pin %s\n' \
            "$(awk '$1 == "commit" { print substr($2, 1, 12) }' \
                "$repo_dir/third_party/merkmal/PINNED")"
    } > "$stamp"
    echo "wasm-provenance: recorded ${current:0:12} (${commit})"
    ;;
--check)
    if [ ! -f "$stamp" ]; then
        echo "wasm-provenance: web/BUILD_INFO is missing; run ./web/build-wasm.sh" >&2
        exit 1
    fi
    recorded="$(awk '$1 == "sources_sha256" { print $2 }' "$stamp")"
    if [ "$recorded" != "$current" ]; then
        cat >&2 <<EOF
wasm-provenance: web/cognator.wasm was built from different sources.

  recorded  $recorded
  sources   $current

The committed artifact is what the page deploys, so rebuild and commit it:

  source ~/emsdk/emsdk_env.sh
  ./web/build-wasm.sh
  git add web/cognator.js web/cognator.wasm web/BUILD_INFO
EOF
        exit 1
    fi
    echo "wasm-provenance: current (${current:0:12})"
    ;;
*)
    echo "usage: $0 [--check|--write]" >&2
    exit 2
    ;;
esac
