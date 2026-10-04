#!/usr/bin/env bash
# Playden - Prepare and build the selected direct ps5-homebrew-ui consumer.
# Copyright (C) 2026 Leo Lobato
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
revision=8a716f294835e26a24ef5612f1535aabffef4080
framework_repo=${PLAYDEN_HUI_REPOSITORY:-"$root/third_party/ps5-homebrew-ui"}
hui_source="$root/build/dependencies/homebrew-ui"
patch_files=("$root/patches/homebrew-ui-macos-4.1.patch" "$root/patches/homebrew-ui-cff-sdf.patch")
build_dir="$root/build/homebrew-host"
font_dir="$root/build/homebrew-fonts"
prepare_only=0
validate_prepared_source=0

if [[ ${1:-} == --prepare-only ]]; then
    prepare_only=1
    shift
elif [[ ${1:-} == --prepare-fonts-only ]]; then
    [[ -n ${2:-} ]] || { echo '--prepare-fonts-only requires a prepared Hui checkout' >&2; exit 1; }
    prepare_only=1
    validate_prepared_source=1
    hui_source=$2
    shift 2
fi

if [[ ! -e $hui_source/.git ]]; then
    if (( validate_prepared_source )); then
        printf 'prepared ps5-homebrew-ui checkout not found: %s\n' "$hui_source" >&2
        exit 1
    fi
    if [[ ! -d $framework_repo/.git && ! -f $framework_repo/.git ]]; then
        printf 'ps5-homebrew-ui repository not found: %s\n' "$framework_repo" >&2
        exit 1
    fi
    mkdir -p "$root/build/dependencies"
    git -C "$framework_repo" worktree add --detach "$hui_source" "$revision"
fi

actual_revision=$(git -C "$hui_source" rev-parse HEAD)
if [[ $actual_revision != "$revision" ]]; then
    printf 'ps5-homebrew-ui revision mismatch: expected %s, found %s\n' \
        "$revision" "$actual_revision" >&2
    exit 1
fi

expected_patch_hashes=(
    8f672e19009e1e63601c4276979aaa31d19b7a5ff4b00184206b89041af66c09
    5af8fe22fa96c9cb2f8f7402fbd9c17d38ac1cadf249bfff2dec42a2623bea1a
)
for index in "${!patch_files[@]}"; do
    actual_hash=$(shasum -a 256 "${patch_files[$index]}" | awk '{print $1}')
    if [[ $actual_hash != "${expected_patch_hashes[$index]}" ]]; then
        printf 'recorded Hui patch hash mismatch: %s\n' "${patch_files[$index]}" >&2
        exit 1
    fi
done

if (( validate_prepared_source )); then
    for patch_file in "${patch_files[@]}"; do
        if ! git -C "$hui_source" apply --check --reverse "$patch_file" >/dev/null 2>&1; then
            printf 'prepared ps5-homebrew-ui checkout lacks recorded patch: %s\n' "$patch_file" >&2
            exit 1
        fi
    done
else
    for patch_file in "${patch_files[@]}"; do
        if git -C "$hui_source" apply --check "$patch_file" >/dev/null 2>&1; then
            git -C "$hui_source" apply "$patch_file"
        elif ! git -C "$hui_source" apply --check --reverse "$patch_file" >/dev/null 2>&1; then
            printf 'isolated ps5-homebrew-ui checkout conflicts with recorded patch: %s\n' "$patch_file" >&2
            git -C "$hui_source" status --short >&2
            exit 1
        fi
    done
fi

# The isolated worktree may contain only the two pinned patch effects.
expected_changes=$' M src/gfx/backdrop.cpp\n M src/gfx/backdrop.hpp\n M src/gfx/canvas.cpp\n M src/gfx/canvas.hpp\n M src/gfx/gl_batch.cpp\n M src/gfx/gl_batch.hpp\n M src/gfx/gl_program.hpp\n M src/gfx/renderer.cpp\n M tools/font-baker/bake_font.cpp\n?? src/gfx/gl.hpp'
actual_changes=$(git -C "$hui_source" status --porcelain --untracked-files=all)
if [[ $actual_changes != "$expected_changes" ]]; then
    printf 'isolated ps5-homebrew-ui checkout has changes outside the pinned patches\n' >&2
    printf '%s\n' "$actual_changes" >&2
    exit 1
fi

python3 - "$root/dependencies.lock.json" "$hui_source" <<'PY'
import hashlib
import json
import pathlib
import sys

lock = json.loads(pathlib.Path(sys.argv[1]).read_text())
source = pathlib.Path(sys.argv[2])
for relative, expected in lock["homebrew_ui"]["patched_files"].items():
    actual = hashlib.sha256((source / relative).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f"pinned Hui patched-file hash mismatch: {relative}")
PY

cxx=$(command -v "${HOST_CXX:-clang++}")
baker="$root/build/hui-bake-font"
mkdir -p "$font_dir"
"$cxx" -std=c++20 -O2 -w \
    "$hui_source/tools/font-baker/bake_font.cpp" -o "$baker"

while IFS='|' read -r input output glyphs; do
    if [[ $glyphs == catalog-european ]]; then
        glyphs="european,text=$root/fixtures/ui-comparison/catalog.json"
    elif [[ $glyphs == catalog ]]; then
        glyphs="text=$root/fixtures/ui-comparison/catalog.json"
    fi
    "$baker" "$root/fixtures/ui-comparison/assets/fonts/$input" \
        "$font_dir/$output" 56 8 2048 "$glyphs" >/dev/null
done <<'FONTS'
Barlow-Regular.ttf|barlow-regular.huifont|european
Barlow-Medium.ttf|barlow-medium.huifont|european
Barlow-SemiBold.ttf|barlow-semibold.huifont|european
BarlowCondensed-Medium.ttf|barlow-condensed-medium.huifont|european
BarlowCondensed-SemiBold.ttf|barlow-condensed-semibold.huifont|european
NotoSansCJKsc-Regular.otf|noto-cjk-fallback.huifont|catalog-european
NotoSansArabic.ttf|noto-arabic-fallback.huifont|catalog
FONTS

# Supplemental font for complete Greek coverage; frozen comparison fixtures stay intact.
"$baker" "$root/assets/fonts/NotoSans-Regular.ttf" \
    "$font_dir/noto-greek-fallback.huifont" 56 8 2048 "european,text=$root/fixtures/ui-comparison/catalog.json" >/dev/null
cp "$root/assets/fonts/NotoSans-OFL.txt" "$font_dir/NotoSans-OFL.txt"

if (( prepare_only )); then
    printf 'Verified ps5-homebrew-ui %s and refreshed font atlases in %s\n' "$revision" "$font_dir"
    exit 0
fi

if [[ ! -f $root/probes/steam-network/.deps/host/lib/libcurl.a ]]; then
    "$root/probes/steam-network/prepare-host-deps.sh"
fi

cmake --preset homebrew-host \
    -DPLAYDEN_HUI_SOURCE_DIR="$hui_source"
cmake --build --preset homebrew-host --target playden-homebrew

if (( $# > 0 )); then
    "$build_dir/playden-homebrew" "$@"
fi
