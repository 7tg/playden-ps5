#!/usr/bin/env bash
# Package candidate B locally, without console discovery, upload or launch.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
environment=${PLAYDEN_PS5_ENV:-/Users/leolobato/Documents/Projetos/Personal/GameNative/playden-ps5/scripts/with-env.sh}
deps=${PLAYDEN_PS5_DEPS_DIR:-/Users/leolobato/Documents/Projetos/Personal/GameNative/rmlfocus/.deps}
hui=${PLAYDEN_HUI_SOURCE_DIR:-"$root/build/dependencies/homebrew-ui"}
[[ -f "$environment" ]] || { echo "Missing explicit PS5 environment: $environment" >&2; exit 1; }
[[ -d "$hui/src" ]] || { echo "Run scripts/build-homebrew.sh to prepare isolated Hui and fonts first" >&2; exit 1; }
bash "$environment" bash -c 'exec "$PS5_PAYLOAD_SDK/bin/prospero-cmake" "$@"' bash \
 -G Ninja -S "$root" -B "$root/build/homebrew-ps5" -DCMAKE_BUILD_TYPE=Release \
 -DPLAYDEN_HOMEBREW_UI=ON -DPLAYDEN_PLATFORM_PS5=ON -DPLAYDEN_PS5_TITLE_ID=FAKE77031 \
 -DPLAYDEN_PS5_DEPS_DIR="$deps" -DPLAYDEN_HUI_SOURCE_DIR="$hui"
bash "$environment" cmake --build "$root/build/homebrew-ps5" --target playden-homebrew-ps5-package
echo "Candidate B package (not deployed): $root/build/homebrew-ps5/dist/FAKE77031"
