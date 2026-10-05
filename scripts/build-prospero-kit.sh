#!/usr/bin/env bash
# Explicit offline packaging only: no Wine build, downloads or console access.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir=${PLAYDEN_PROSPERO_SOURCE_DIR:-"$root/third_party/prospero-win"}
python_bin=${PLAYDEN_PYTHON:-python3}
source_args=(--source "$source_dir")
if [[ ${1:-} == --release ]]; then
 source_args+=(--release)
 shift
fi
if [[ $# -eq 0 ]]; then
 echo 'Usage: build-prospero-kit.sh [--release] package --spec SPEC --out KIT' >&2
 echo '       build-prospero-kit.sh [--release] validate KIT' >&2
 exit 2
fi
"$python_bin" "$root/tools/prospero_source.py" "${source_args[@]}"
exec "$python_bin" "$source_dir/tools/package_playden_kit.py" "$@"
