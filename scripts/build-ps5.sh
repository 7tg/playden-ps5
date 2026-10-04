#!/usr/bin/env bash
# Build and package locally. This script has no upload, launch, or console-discovery code.
set -euo pipefail

usage() {
    echo "usage: $0 --title-id FAKE12345 [--rmlfocus-source PATH] [--developer-fixtures] [--capture-after SECONDS]" >&2
    exit 2
}

capture_after=0
developer_fixtures=OFF
title_id=
rmlfocus_source=
while [[ $# -gt 0 ]]; do
    case "$1" in
        --capture-after) [[ $# -ge 2 ]] || usage; capture_after=$2; shift 2 ;;
        --developer-fixtures) developer_fixtures=ON; shift ;;
        --title-id) [[ $# -ge 2 ]] || usage; title_id=$2; shift 2 ;;
        --rmlfocus-source) [[ $# -ge 2 ]] || usage; rmlfocus_source=$2; shift 2 ;;
        *) usage ;;
    esac
done
[[ "$title_id" =~ ^FAKE[0-9]{5}$ ]] || usage
[[ "$capture_after" =~ ^(0|[1-9][0-9]*)$ ]] || usage

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
rmlfocus_source=${rmlfocus_source:-"$root/../rmlfocus"}
environment="$root/../playden-ps5/scripts/with-env.sh"
[[ -f "$environment" ]] || { echo "missing PS5 environment wrapper: $environment" >&2; exit 1; }

bash "$environment" "$root/scripts/prepare-rmlfocus.sh" ps5 "$rmlfocus_source" "$root/build/ps5"
bash "$environment" bash -c 'exec "$PS5_PAYLOAD_SDK/bin/prospero-cmake" "$@"' bash \
    -G Ninja -S "$root" -B "$root/build/ps5" -DCMAKE_BUILD_TYPE=Release \
    -DPLAYDEN_PLATFORM_PS5=ON -DPLAYDEN_PS5_TITLE_ID="$title_id" \
    -DPLAYDEN_DEVELOPER_FIXTURES="$developer_fixtures" -DPLAYDEN_CAPTURE_AFTER_SECONDS="$capture_after" \
    -DPLAYDEN_RMLFOCUS_SOURCE_DIR="$rmlfocus_source"
bash "$environment" cmake --build "$root/build/ps5" --target playden-ps5-package
echo "Package: $root/build/ps5/dist/$title_id"
