#!/usr/bin/env bash
# Playden - Prepare and build the direct ps5-homebrew-ui candidate.
# Copyright (C) 2026 Leo Lobato
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
revision=8a716f294835e26a24ef5612f1535aabffef4080
framework_repo=${PLAYDEN_HUI_REPOSITORY:-/Users/leolobato/Documents/Projetos/Personal/GameNative/ps5-homebrew/ps5-homebrew-ui}
hui_source="$root/build/dependencies/homebrew-ui"
patch_files=("$root/patches/homebrew-ui-macos-4.1.patch" "$root/patches/homebrew-ui-cff-sdf.patch")
build_dir="$root/build/homebrew-host"
font_dir="$root/build/homebrew-fonts"
prepare_only=0

if [[ ${1:-} == --prepare-only ]]; then
    prepare_only=1
    shift
fi

if [[ ! -d $framework_repo/.git && ! -f $framework_repo/.git ]]; then
    printf 'ps5-homebrew-ui repository not found: %s\n' "$framework_repo" >&2
    exit 1
fi

if [[ ! -e $hui_source/.git ]]; then
    mkdir -p "$root/build/dependencies"
    git -C "$framework_repo" worktree add --detach "$hui_source" "$revision"
fi

actual_revision=$(git -C "$hui_source" rev-parse HEAD)
if [[ $actual_revision != "$revision" ]]; then
    printf 'ps5-homebrew-ui revision mismatch: expected %s, found %s\n' \
        "$revision" "$actual_revision" >&2
    exit 1
fi

for patch_file in "${patch_files[@]}"; do
    if git -C "$hui_source" apply --check "$patch_file" >/dev/null 2>&1; then
        git -C "$hui_source" apply "$patch_file"
    elif ! git -C "$hui_source" apply --check --reverse "$patch_file" >/dev/null 2>&1; then
        printf 'isolated ps5-homebrew-ui checkout conflicts with recorded patch: %s\n' "$patch_file" >&2
        git -C "$hui_source" status --short >&2
        exit 1
    fi
done

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

if (( prepare_only )); then
    printf 'Prepared ps5-homebrew-ui %s and Barlow atlases in %s\n' "$revision" "$font_dir"
    exit 0
fi

cmake -S "$root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DPLAYDEN_HOMEBREW_UI=ON \
    -DPLAYDEN_PLATFORM_PS5=OFF \
    -DPLAYDEN_HUI_SOURCE_DIR="$hui_source"
cmake --build "$build_dir" --target playden-homebrew

if (( $# > 0 )); then
    "$build_dir/playden-homebrew" "$@"
fi
