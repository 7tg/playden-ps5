#!/usr/bin/env bash
# Build the relaunch payload (tools/self-update-relaunch) that the PS5 package embeds for the self-update.
# Reproducible like the title remover: compiled by relative name from its own folder. Its SHA-256 is pinned in
# dependencies.lock.json (self_update_relaunch).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/self-update-relaunch"
mkdir -p "$out"
(cd "$root/tools/self-update-relaunch" && "$root/scripts/with-env.sh" bash -c \
    'exec "$PS5_PAYLOAD_SDK/bin/prospero-clang" -Wall -Werror -O2 -ffile-prefix-map="$1"=. -o "$2" main.c \
         -lSceUserService -lSceSystemService' bash "$root" "$out/self-update-relaunch.elf")
echo "Relaunch payload: $out/self-update-relaunch.elf ($(shasum -a 256 "$out/self-update-relaunch.elf" | cut -c1-16)…)"
