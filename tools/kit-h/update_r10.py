#!/usr/bin/env python3
"""Kit 0.10: kit H r9 plus the static media backend (winegstreamer with GStreamer and FFmpeg, LGPL) and the Wine fixes since.

The prospero-win commit adds these Wine patches to r9's: 0480-0481 build winegstreamer with static plugins and make it fail
cleanly, 0482 adds rtworkq cleanup groups, 0964 retires thread-alert kqueues in ntdll's Unix side, 0965 stops win32u's pixel
format message loop on the desktop, 0970 traces each process query class once, 0971 ends a crashed title instead of starting a
debugger, 0972 keeps FAudio's resampler inside its buffer (xaudio2_*, xactengine*), 0980 dumps long-waiting stacks in the
wineserver watchdog and 0981 registers win32u's builtin classes without waiting on another thread. The media build also maps
build paths out of every module (-ffile-prefix-map), so every cross PRX and PE module comes out different from r9's, and all of
them are replaced. What is checked instead: the build's Wine commit and patch list are r9's plus exactly those patches, and
the build makes no module r9 lacks besides winegstreamer (rtworkq.dll is now cross-built; r9 had the host build's). The
template's copies follow the runtime, and the template gets the media registry values.

Copy kit H r9's inputs and kit-spec.json to the new kit folder first (cp -c works). Build with build_wine_ps5.sh
--media-root (build_cross.sh with PLAYDEN_MEDIA_WORK set), strip the cross PE modules except winegstreamer.dll with
*-mingw32-strip -s (its hash is the registered one), and register both winegstreamer.dll builds in a disposable prefix
(README.md, Media backend). --base-report is r9's cross build report.json. --name sets the kit version in the artifact
revision, so the same script stages a development kit (0.10-dev.YYYYMMDD.N) and the release (0.10).
"""
import argparse, json, re, shutil
from pathlib import Path
from media_staging import validate_media, stage_media, sha, write_json

WINE_PATCHES = ['0480-winegstreamer-ps5-static-plugins.patch', '0481-winegstreamer-media-failure.patch',
                '0482-rtworkq-cleanup-groups.patch', '0964-ntdll-ps5-retire-thread-alert-kqueues.patch',
                '0965-win32u-ps5-no-pixel-format-message-loop-on-the-desktop.patch',
                '0970-ntdll-ps5-trace-each-process-query-class-once.patch', '0971-kernelbase-ps5-no-debugger-in-a-title.patch',
                '0972-faudio-keep-resampling-taps-within-the-samples.patch',
                '0980-server-ps5-dump-long-waiting-stacks-in-the-watchdog.patch',
                '0981-win32u-register-builtin-classes-without-waiting.patch']
NEW = {'x86_64-unix': {'winegstreamer.prx'}, 'i386-windows': {'winegstreamer.dll'}, 'x86_64-windows': {'winegstreamer.dll'}}
REQUIRED = ['x86_64-unix/ntdll.prx', 'x86_64-unix/win32u.prx', 'x86_64-unix/wineserver.prx', 'i386-windows/rtworkq.dll',
            'x86_64-windows/rtworkq.dll', 'i386-windows/kernelbase.dll', 'x86_64-windows/kernelbase.dll',
            'i386-windows/xaudio2_7.dll', 'x86_64-windows/xaudio2_7.dll']


def cross_modules(cross):
    yield 'x86_64-unix', sorted(p for p in (cross / 'prx/sce_module').glob('*.prx') if p.is_file())
    for arch in ('i386-windows', 'x86_64-windows'):
        yield arch, sorted(p for p in (cross / 'pe' / arch).glob('*') if p.is_file())


def check_build(runtime, cross, base_report):
    """The cross modules that replace r9's, after checking the build is r9's plus the kit's patches."""
    report = json.loads((cross / 'report.json').read_text())
    if report['wine_commit'] != base_report['wine_commit']:
        raise ValueError('Wine commit differs from r9')
    added, removed = set(report['patches']) - set(base_report['patches']), set(base_report['patches']) - set(report['patches'])
    if added != set(WINE_PATCHES) or removed:
        raise ValueError(f'patches differ from r9 plus the kit patches: added {sorted(added)}, removed {sorted(removed)}')
    replace, unexpected = [], []
    for arch, sources in cross_modules(cross):
        for source in sources:
            if source.name in NEW[arch]:
                continue
            module = f'{arch}/{source.name}'
            (replace if (runtime / 'lib/wine' / module).is_file() else unexpected).append(module)
    if unexpected:
        raise ValueError(f'cross modules r9 does not have: {unexpected}')
    missing = [m for m in REQUIRED if m not in replace]
    if missing:
        raise ValueError(f'cross modules missing from the build: {missing}')
    return replace


def stage(here, previous, cross, registration, media_work, base_report, revision, name='0.10'):
    here, previous, cross, registration, media_work = (Path(p).resolve() for p in (here, previous, cross, registration, media_work))
    if here == previous or previous in here.parents:
        raise ValueError('the kit must be a separate copy of r9')
    if not re.fullmatch('[0-9a-f]{40}', revision):
        raise ValueError('full runtime commit required')
    if not re.fullmatch(r'0\.10(-dev\.[0-9]{8}\.[0-9]+)?', name):
        raise ValueError('name is 0.10 or 0.10-dev.YYYYMMDD.N')
    evidence = validate_media(cross, registration, media_work)
    spec_path = here / 'kit-spec.json'
    spec = json.loads(spec_path.read_text().replace(str(previous) + '/', str(here) + '/'))
    if not spec['runtime']['artifact_revision'].endswith('-kit-h-r9'):
        raise ValueError('kit 0.10 starts from r9')
    record_path = here / 'inputs/stage-record.json'
    record = json.loads(record_path.read_text())
    if '0.10' in record or 'r10' in record:
        raise ValueError('kit 0.10 already staged')
    runtime, template = here / 'inputs/runtime/wine', here / 'inputs/template/game'
    replace = check_build(runtime, cross, json.loads(Path(base_report).read_text()))
    substitutions_path = here / 'inputs/template-substitutions.json'
    substitutions = json.loads(substitutions_path.read_text())

    # stage_media checks everything else before it writes; the module swaps follow it.
    media = stage_media(runtime, template, cross, registration, media_work, substitutions['files'], evidence)
    changed = []
    for module in replace:
        arch, file = module.split('/')
        source = cross / ('prx/sce_module' if arch == 'x86_64-unix' else 'pe/' + arch) / file
        target = runtime / 'lib/wine' / module
        if sha(source) == sha(target):
            continue
        mode = target.stat().st_mode & 0o7777
        target.unlink()  # the inputs may be a copy-on-write clone of r9's
        shutil.copyfile(source, target); target.chmod(mode)
        changed.append(f'lib/wine/{module}')
    template_changed = []
    for entry in substitutions['files']:
        if entry['runtime_source'] in changed:
            target = template / entry['template_path']
            if sha(target) != sha(runtime / entry['runtime_source']):
                target.unlink()
                shutil.copyfile(runtime / entry['runtime_source'], target); target.chmod(0o644)
            entry['after_sha256'] = sha(target)
            template_changed.append(entry['template_path'])

    artifact = f'{revision[:7]}-kit-{name}'
    spec['runtime'].update(wine_revision=revision, artifact_revision=artifact)
    spec['sources']['wine']['revision'] = revision
    spec['sources']['media'] = {'wine_commit': evidence['report']['wine_commit'], 'registry_idl_sha256': sha(evidence['idl']),
                                'playback_verified': False}
    write_json(spec_path, spec)
    substitutions['runtime_artifact_revision'] = artifact
    substitutions['count'] = len(substitutions['files'])
    substitutions['changed'] = sum(e['before_sha256'] != e['after_sha256'] for e in substitutions['files'])
    write_json(substitutions_path, substitutions)
    write_json(here / 'inputs/runtime-inventory.json',
               {'wine/' + p.relative_to(runtime).as_posix(): sha(p) for p in sorted(runtime.rglob('*')) if p.is_file()})
    record['0.10'] = {'wine_patches': [p.removesuffix('.patch') for p in WINE_PATCHES], 'changed': changed, 'template_changed': template_changed,
                     'media': media, 'base_runtime': record['artifact_revision']}
    record['cross_pe_modules'] = sorted(set(record.get('cross_pe_modules', [])) |
                                        {c.removeprefix('lib/wine/') for c in changed + media['changed'] if c.endswith('.dll')})
    record['prospero_win'] = revision
    record['artifact_revision'] = artifact
    write_json(record_path, record)
    return record['0.10']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for option in ('kit', 'from-kit', 'cross', 'registration', 'media-work', 'base-report', 'revision'):
        parser.add_argument('--' + option, required=True)
    parser.add_argument('--name', default='0.10')
    a = parser.parse_args()
    print(json.dumps(stage(a.kit, a.from_kit, a.cross, a.registration, a.media_work, a.base_report, a.revision, a.name), indent=2))
