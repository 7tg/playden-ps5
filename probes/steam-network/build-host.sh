#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
"$root/prepare-host-deps.sh"
rm -rf "$root/build/host"
cmake -S "$root" -B "$root/build/host" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$root/.deps/host"
cmake --build "$root/build/host"
echo "Run: $root/build/host/playden-steam-network-probe --ca-bundle $root/.deps/host/cacert.pem"
