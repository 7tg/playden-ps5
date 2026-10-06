#!/usr/bin/env bash
# Local-only build; run through playden-ps5/scripts/with-env.sh after the native package.
set -euo pipefail
cd "$(dirname "$0")/.."
out=build/title-contract
native=build/homebrew-ps5/host-tools/native-src
mkdir -p "$out/FAKE78000/sce_sys"
"$PS5_PAYLOAD_SDK/bin/prospero-clang" -O2 -Wall -c probes/title-contract/main.c -o "$out/main.o"
"$PS5_PAYLOAD_SDK/bin/prospero-lld" -T "$native/ps5-pie.ld" --eh-frame-hdr \
  --version-script "$native/app-symbols.map" -e _start -o "$out/llvm-pie.elf" \
  "$out/main.o" --as-needed "$PS5_PAYLOAD_SDK/target/lib/libkernel.so" \
  "$PS5_PAYLOAD_SDK/target/lib/libSceSystemService.so"
build/homebrew-ps5/host-tools/ps5-native-tool link --in "$out/llvm-pie.elf" --out "$out/eboot.elf" \
  --stub-dir build/homebrew-ps5/stubs --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
build/homebrew-ps5/host-tools/ps5-native-tool self --sign --in "$out/eboot.elf" \
  --out "$out/FAKE78000/eboot.bin" --magic 0x1D3D154F
python3 - <<'PY'
import json, pathlib, shutil
src = pathlib.Path('build/homebrew-ps5/dist/FAKE77031/sce_sys')
dst = pathlib.Path('build/title-contract/FAKE78000/sce_sys')
p = json.loads((src / 'param.json').read_text())
p.update(titleId='FAKE78000', conceptId='78000', contentId='UP9000-FAKE78000_00-PLAYDENPROBE0001')
p['localizedParameters']['en-US']['titleName'] = 'Playden Contract Probe'
(dst / 'param.json').write_text(json.dumps(p, indent=2) + '\n')
shutil.copy2(src / 'icon0.png', dst / 'icon0.png')
PY
