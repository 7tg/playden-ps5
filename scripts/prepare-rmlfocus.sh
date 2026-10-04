#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <host|ps5> <rmlfocus-source> <launcher-build-dir>" >&2
    exit 2
fi

platform=$1
rmlfocus_source=$(cd "$2" && pwd)
launcher_build=$3
rmlfocus_expected=31d2d8fd39ca3e3bb3ba91a43f50561c04429d57
actual=$(git -C "$rmlfocus_source" rev-parse HEAD)
if [[ "$actual" != "$rmlfocus_expected" ]]; then
    echo "RmlFocus must be revision $rmlfocus_expected; found $actual in $rmlfocus_source" >&2
    exit 1
fi
if ! git -C "$rmlfocus_source" diff --quiet HEAD -- || ! git -C "$rmlfocus_source" diff --cached --quiet; then
    echo "RmlFocus has tracked local changes; refusing a build that would not match the locked revision" >&2
    exit 1
fi

while read -r relative dependency_expected; do
    checkout="$rmlfocus_source/$relative"
    actual=$(git -C "$checkout" rev-parse HEAD)
    if [[ "$actual" != "$dependency_expected" ]] || ! git -C "$checkout" diff --quiet HEAD -- || ! git -C "$checkout" diff --cached --quiet; then
        echo "$relative must be a clean checkout at $dependency_expected; found $actual" >&2
        exit 1
    fi
done <<'PINS'
.deps/RmlUi ba95ffe8bfb6370efb2cdcca927eaad4710c5413
.deps/freetype 42608f77f20749dd6ddc9e0536788eaad70ea4b5
.deps/ps5-native-app-boilerplate 4f531c4b517f80bcb6b1267135848168d2250047
.deps/ps5-opengl-src dce74910af7f4274c48d05e0ae001b3038462588
PINS

dependency_build="$launcher_build/dependencies/rmlfocus-$platform"
prefix="$launcher_build/dependencies/$platform-prefix"
stamp="$prefix/.rmlfocus-revision"
if [[ -f "$stamp" && "$(<"$stamp")" == "$rmlfocus_expected" && -f "$prefix/lib/cmake/RmlFocus/RmlFocusConfig.cmake" ]]; then
    exit 0
fi

common=(-G Ninja -S "$rmlfocus_source" -B "$dependency_build"
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
    -DRF_BUILD_APPS=ON -DRF_BUILD_TESTS=OFF -DRF_BUILD_HUI=OFF -DRF_VISUAL_CHECKS=OFF)
if [[ "$platform" == host ]]; then
    cmake "${common[@]}"
else
    if [[ "$platform" != ps5 || -z "${PS5_PAYLOAD_SDK:-}" ]]; then
        echo "PS5_PAYLOAD_SDK is required for a PS5 dependency build" >&2
        exit 1
    fi
    "$PS5_PAYLOAD_SDK/bin/prospero-cmake" "${common[@]}" -DRF_PLATFORM_PS5=ON
fi
cmake --build "$dependency_build" --target rmlfocus_rmlui
cmake --install "$dependency_build"
printf '%s\n' "$rmlfocus_expected" > "$stamp"
