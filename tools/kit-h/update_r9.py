#!/usr/bin/env python3
# Kit H r9: kit H r8 with wined3d.dll (i386 and x86_64) rebuilt from the prospero-win commit that adds Wine patches 0959-0963:
# vkd3d-shader writes fx_2_0 effects with in-pass shaders, sampler states and parameter references, accepts TRUE/FALSE for any
# state, writes NULL shader and texture states, and compiles ps_1_x effect shaders as ps_2_0, so D3DXCreateEffect builds the
# effects of DoDonPachi Resurrection and Star Wars: Empire at War. wined3d links vkd3d-shader, so it is the module that changes,
# in the runtime and in the template's system32/syswow64; every cross PRX stays r8's, and bottles need no Recreate.
# Copy kit H r8's inputs and kit-spec.json to PLAYDEN_KIT_DIR first (cp -c works). Build with build_cross.sh's arguments into
# the same wine-ps5 work folder as r8, then run this script with PLAYDEN_CROSS_PE at that build's pe folder (copied to the
# Mac), PLAYDEN_CROSS_PRX at its prx/sce_module and PLAYDEN_RUNTIME_REVISION set to that commit. The PE copies come out
# unstripped: strip them with *-mingw32-strip -s, as build_host_wine.sh does, before running this.
import hashlib, json, os, shutil
from pathlib import Path

work = Path(os.environ.get('PLAYDEN_KIT_WORK', Path.home() / 'playden-kit'))
pe = Path(os.environ.get('PLAYDEN_CROSS_PE', work / 'wine-ps5/pe')).resolve()
cross = Path(os.environ.get('PLAYDEN_CROSS_PRX', work / 'wine-ps5/prx/sce_module')).resolve()
here = Path(os.environ.get('PLAYDEN_KIT_DIR', 'build/kit-h-r9')).resolve()
previous = Path(os.environ.get('PLAYDEN_KIT_FROM', 'build/kit-h-r8')).resolve()
runtime = here / 'inputs/runtime/wine'
template = here / 'inputs/template/game'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def replace(source, target):
    mode = target.stat().st_mode & 0o7777
    target.unlink()  # the inputs may be a copy-on-write clone of r8's
    shutil.copyfile(source, target); target.chmod(mode)

spec_path = here / 'kit-spec.json'
spec = json.loads(spec_path.read_text().replace(str(previous) + '/', str(here) + '/'))
assert spec['runtime']['artifact_revision'].endswith('-kit-h-r8'), 'r9 starts from r8'

# Every cross PRX must still be r8's.
drifted = [p.name for p in sorted(cross.glob('*.prx')) if sha(p) != sha(runtime / 'lib/wine/x86_64-unix' / p.name)]
assert not drifted, f'cross PRXs that differ from r8: {drifted}'

changed = []
for arch in ('i386-windows', 'x86_64-windows'):
    source, target = pe / arch / 'wined3d.dll', runtime / 'lib/wine' / arch / 'wined3d.dll'
    assert source.is_file() and target.is_file(), (source, target)
    if sha(source) != sha(target):
        replace(source, target)
    changed.append(f'lib/wine/{arch}/wined3d.dll')
substitutions = json.loads((here / 'inputs/template-substitutions.json').read_text())
template_changed = []
for entry in substitutions['files']:
    if entry['runtime_source'] in changed:
        target = template / entry['template_path']
        if sha(target) != sha(runtime / entry['runtime_source']):
            replace(runtime / entry['runtime_source'], target)
        entry['after_sha256'] = sha(target)
        template_changed.append(entry['template_path'])
assert len(template_changed) == len(changed), template_changed
inventory = {f'wine/{p.relative_to(runtime)}': sha(p) for p in sorted(runtime.rglob('*')) if p.is_file()}
(here / 'inputs/runtime-inventory.json').write_text(json.dumps(inventory, indent=1, sort_keys=True) + '\n')

revision = os.environ['PLAYDEN_RUNTIME_REVISION']
assert len(revision) == 40, revision
artifact = f'{revision[:7]}-kit-h-r9'
spec['runtime'].update(wine_revision=revision, artifact_revision=artifact)
spec['sources']['wine']['revision'] = revision
spec_path.write_text(json.dumps(spec, indent=1, sort_keys=True) + '\n')

substitutions['runtime_artifact_revision'] = artifact
substitutions['changed'] = sum(f['before_sha256'] != f['after_sha256'] for f in substitutions['files'])
(here / 'inputs/template-substitutions.json').write_text(json.dumps(substitutions, indent=1) + '\n')
record = json.loads((here / 'inputs/stage-record.json').read_text())
record['r9'] = {'wine_patches': ['0959-vkd3d-shader-fx-implement-pass-assignment-for-fx-2-0',
                                 '0960-vkd3d-shader-fx-write-sampler-state-initializers-for-fx-2-0',
                                 '0961-vkd3d-shader-fx-accept-true-and-false-for-any-fx-2-0-state',
                                 '0962-vkd3d-shader-fx-write-null-shader-and-texture-states-for-fx-2-0',
                                 '0963-vkd3d-shader-hlsl-compile-ps-1-x-effect-shaders-as-ps-2-0'],
                'changed': changed, 'template_changed': template_changed}
record['cross_pe_modules'] = sorted(set(record.get('cross_pe_modules', [])) | {c.removeprefix('lib/wine/') for c in changed})
record['prospero_win'] = revision
record['artifact_revision'] = artifact
(here / 'inputs/stage-record.json').write_text(json.dumps(record, indent=1) + '\n')
print('changed', changed, 'template', template_changed, 'files', len(inventory), artifact)
