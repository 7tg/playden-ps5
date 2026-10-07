#!/usr/bin/env bash
# Kit C: a base kit's runtime, template and recipe with ONE any-title shell (title-any-id).
# Usage: BASE=kit-a|kit-b make_kit.sh  (builds from the prospero-win and Lapy worktrees' HEADs)
#   kit-a: build/kit-a-6a77259 (kit-spec-shared.json) with prospero-win feat/title-shells-kit-a
#   kit-b: build/kit-b-25a967d (kit-spec.json) with prospero-win feat/title-shells
set -euo pipefail
R=$(git rev-parse --show-toplevel)
here=$R/build/kit-c
P=/Users/leolobato/Documents/Projetos/Personal/playden/ps5/playden-ps5
case ${BASE:-kit-b} in
    kit-a) base=$P/build/kit-a-6a77259; spec_in=$base/kit-spec-shared.json; pw=$R/build/wt/prospero-win-kit-a ;;
    kit-b) base=$P/build/kit-b-25a967d; spec_in=$base/kit-spec.json; pw=$R/build/wt/prospero-win ;;
    *) echo "BASE must be kit-a or kit-b" >&2; exit 2 ;;
esac
lapy=$R/build/wt/lapy
rev=$(git -C "$pw" rev-parse HEAD)
[[ -z $(git -C "$pw" status --porcelain) && -z $(git -C "$lapy" status --porcelain) ]] || { echo "dirty worktree" >&2; exit 2; }
out=$here/kit-c-${BASE:-kit-b}-${rev:0:7}
[[ ! -e $out ]] || { echo "$out exists" >&2; exit 2; }
mkdir -p "$out/inputs/titles" "$out/logs"
# Rule helper, then the shell built against it.
(cd "$lapy" && PS5_PAYLOAD_SDK=$P/deps/ps5-payload-sdk-helper "$R/scripts/with-env.sh" python3 tools/build_owned_daemon.py \
    --sdk "$P/deps/ps5-payload-sdk-helper" --logging-client "$pw/native/ps5log" --elf-helper \
    --title-rule FAKE78 --allow-list /data/playden/title-rule/allow-list) > "$out/logs/helper.log"
helper=$lapy/build/owned_root_helper-FAKE78-rule
(cd "$pw" && PS5_NATIVE_FOUNDATION=/Users/leolobato/Documents/Projetos/Personal/GameNative/playden-ps5/.deps/prospero-foundation \
    PW_FOUNDATION_READY=1 PW_ANY_TITLE=1 PW_TITLE_ID=FAKE78000 PW_OUTPUT_SUFFIX=-kit-c \
    PW_LAPY_HELPER_ELF=$helper/lapy.elf PW_LAPY_HELPER_MANIFEST=$helper/lapy-manifest.json \
    "$R/scripts/with-env.sh" bash tools/build_native.sh) > "$out/logs/title.log" 2>&1
title=$out/inputs/titles/any-${rev:0:7}
cp -R "$pw/dist/FAKE78000-kit-c" "$title"
cp "$lapy/source/lapy_elevation_protocol.h" "$out/inputs/"
python3 - "$spec_in" "$out/kit-spec.json" "$title" "$rev" \
    "$(git -C "$lapy" rev-parse HEAD)" "$out/inputs/lapy_elevation_protocol.h" <<'PY'
import json, sys
spec = json.load(open(sys.argv[1]))
out, title, rev, helper_rev, protocol = sys.argv[2:]
spec["variants"] = [{
    "id": "any-title-" + rev[:7], "title_id": "FAKE78000", "any_title": True, "path": title,
    "wrapper_revision": rev, "helper_revision": helper_rev,
    "helper_manifest": title + "/lapy-manifest.json", "helper_protocol": protocol,
    "native_build_manifest": title + "/native-title-build.json",
}]
spec["sources"]["wrapper"]["revision"] = rev
spec["sources"]["helper"]["revision"] = helper_rev
spec["sources"]["helper"]["base_revision"] = "b48b7d7236eca25c7b9dfd6c040c763ae05dde80"  # Lapy upstream v0.3.2
spec["capabilities"] = sorted(set(spec["capabilities"]) | {"title-any-id"})
json.dump(spec, open(out, "w"), indent=2, sort_keys=True)
PY
(cd "$pw" && python3 tools/package_playden_kit.py package --spec "$out/kit-spec.json" --out "$out/kit") | tee "$out/logs/package.log"
(cd "$pw" && python3 tools/package_playden_kit.py validate "$out/kit") | tee -a "$out/logs/package.log"
shasum -a 256 "$out/kit/manifest.json" | tee -a "$out/logs/package.log"
