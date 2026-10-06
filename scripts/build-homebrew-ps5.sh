#!/usr/bin/env bash
# Package the selected direct Hui consumer locally, without console discovery, upload or launch.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
environment=${PLAYDEN_PS5_ENV:-/Users/leolobato/Documents/Projetos/Personal/GameNative/playden-ps5/scripts/with-env.sh}
deps=${PLAYDEN_PS5_DEPS_DIR:-/Users/leolobato/Documents/Projetos/Personal/GameNative/rmlfocus/.deps}
hui=${PLAYDEN_HUI_SOURCE_DIR:-"$root/build/dependencies/homebrew-ui"}
simulation=${PLAYDEN_UI_SIMULATION:-OFF}
native_access=${PLAYDEN_NATIVE_RUNTIME_ACCESS:-ON}
prospero_source=${PLAYDEN_PROSPERO_SOURCE_DIR:-"$root/third_party/prospero-win"}
lapy_helper=${PLAYDEN_LAPY_HELPER_ELF:-}
lapy_manifest=${PLAYDEN_LAPY_HELPER_MANIFEST:-}
case "$simulation" in
 ON|on|1|true) simulation=ON ;;
 OFF|off|0|false|'') simulation=OFF ;;
 *) echo "PLAYDEN_UI_SIMULATION must be ON or OFF" >&2; exit 2 ;;
esac
case "$native_access" in
 ON|on|1|true) native_access=ON ;;
 OFF|off|0|false) native_access=OFF ;;
 *) echo "PLAYDEN_NATIVE_RUNTIME_ACCESS must be ON or OFF" >&2; exit 2 ;;
esac
[[ -f "$environment" ]] || { echo "Missing explicit PS5 environment: $environment" >&2; exit 1; }
[[ -d "$hui/src" ]] || { echo "Run scripts/build-homebrew.sh to prepare isolated Hui and fonts first" >&2; exit 1; }
# PS5 CMake consumes the pre-baked atlas directory directly. Refresh it from the exact selected
# checkout so catalog, font, license, baker, or patch changes cannot leave stale package inputs.
"$root/scripts/build-homebrew.sh" --prepare-fonts-only "$hui"
if [[ ! -f "$root/probes/steam-network/.deps/ps5/lib/libcurl.a" ]]; then
 "$root/probes/steam-network/prepare-ps5-deps.sh"
fi
bash "$environment" bash -c 'exec "$PS5_PAYLOAD_SDK/bin/prospero-cmake" "$@"' bash \
 -G Ninja -S "$root" -B "$root/build/homebrew-ps5" -DCMAKE_BUILD_TYPE=Release \
 -DPLAYDEN_PLATFORM_PS5=ON -DPLAYDEN_PS5_TITLE_ID=FAKE77031 \
 -DPLAYDEN_UI_SIMULATION="$simulation" \
 -DPLAYDEN_NATIVE_RUNTIME_ACCESS="$native_access" -DPLAYDEN_PROSPERO_SOURCE_DIR="$prospero_source" \
 -DPLAYDEN_LAPY_HELPER_ELF="$lapy_helper" -DPLAYDEN_LAPY_HELPER_MANIFEST="$lapy_manifest" \
 -DPLAYDEN_PS5_DEPS_DIR="$deps" -DPLAYDEN_HUI_SOURCE_DIR="$hui"
bash "$environment" cmake --build "$root/build/homebrew-ps5" --target playden-homebrew-ps5-package
echo "Selected Hui package (not deployed): $root/build/homebrew-ps5/dist/FAKE77031"
