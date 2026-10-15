#!/usr/bin/env python3
"""Incomplete native audits must fail before staging mutates kit inputs."""
import importlib.util
import hashlib
import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools/kit-h"
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("update_r10", TOOLS / "update_r10.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MediaStagingTests(unittest.TestCase):
    def test_corrupt_binary_evidence_is_rejected_before_writes(self):
        for defect, error in (("prx", "PRX differs"), ("architecture", "wrong PE architecture"),
                              ("registration", "registered DLL/build report mismatch")):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                kit, previous, cross, registration = (root / name for name in ("r10", "r9", "cross", "registration"))
                (kit / "inputs").mkdir(parents=True)
                previous.mkdir()
                (cross / "prx/sce_module").mkdir(parents=True)
                registration.mkdir()
                original = {kit / "kit-spec.json": "{}\n", kit / "inputs/stage-record.json": "{}\n"}
                for path, value in original.items():
                    path.write_text(value)
                prx = cross / "prx/sce_module/winegstreamer.prx"
                prx.write_bytes(b"native module fixture")
                entry = {"built": True, "sha256": module.sha(prx), **{name: [] for name in
                         ("unresolved", "errors", "title_unbound", "webkit_unbound", "raw_syscalls")}}
                if defect == "prx":
                    prx.write_bytes(b"corrupt module")
                pe_hashes = {}
                registered = {}
                for architecture, machine in (("i386-windows", 0x14c), ("x86_64-windows", 0x8664)):
                    data = bytearray(128)
                    data[:2] = b"MZ"
                    struct.pack_into("<I", data, 60, 64)
                    data[64:68] = b"PE\0\0"
                    struct.pack_into("<H", data, 68, 0x8664 if defect == "architecture" else machine)
                    path = cross / "pe" / architecture / "winegstreamer.dll"
                    path.parent.mkdir(parents=True)
                    path.write_bytes(data)
                    digest = hashlib.sha256(data).hexdigest()
                    pe_hashes[architecture + "/winegstreamer.dll"] = digest
                    registered[architecture] = "0" * 64 if defect == "registration" else digest
                (registration / "dll-sha256.json").write_text(json.dumps(registered))
                (cross / "report.json").write_text(json.dumps({"prx": {"modules": {"winegstreamer": entry}}, "pe": pe_hashes}))
                with self.assertRaisesRegex(ValueError, error):
                    module.stage(kit, previous, cross, registration, root / "media", root / "r9-report.json", "a" * 40)
                self.assertEqual({p: p.read_text() for p in original}, original)

    def test_incomplete_or_malformed_audit_is_rejected_before_writes(self):
        fields = ("unresolved", "errors", "title_unbound", "webkit_unbound", "raw_syscalls")
        for field in fields:
            for invalid in ("missing", None, {}, "", ["failure"]):
                with self.subTest(field=field, invalid=invalid), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    kit, previous, cross = (root / name for name in ("r10", "r9", "cross"))
                    (kit / "inputs").mkdir(parents=True)
                    previous.mkdir()
                    cross.mkdir()
                    original = {kit / "kit-spec.json": "{}\n",
                                kit / "inputs/stage-record.json": "{}\n"}
                    for path, value in original.items():
                        path.write_text(value)
                    entry = {"built": True, **{name: [] for name in fields}}
                    if invalid == "missing":
                        del entry[field]
                    else:
                        entry[field] = invalid
                    (cross / "report.json").write_text(json.dumps({"prx": {"modules": {"winegstreamer": entry}}}))
                    with self.assertRaisesRegex(ValueError, "media PRX.*audit"):
                        module.stage(kit, previous, cross, root / "registration", root / "media", root / "r9-report.json", "a" * 40)
                    self.assertEqual({p: p.read_text() for p in original}, original)

# These fixtures contain no proprietary media or executable code.
class NormalMediaStagingTests(unittest.TestCase):
    def fixture(self, root):
        cross, registration, media = (root / name for name in ('cross', 'registration', 'media'))
        (cross / 'prx/sce_module').mkdir(parents=True)
        registration.mkdir()
        prx = cross / 'prx/sce_module/winegstreamer.prx'
        prx.write_bytes(b'fixture PRX')
        entry = {'built': True, 'sha256': module.sha(prx), **{k: [] for k in
                 ('unresolved', 'errors', 'title_unbound', 'webkit_unbound', 'raw_syscalls')}}
        hashes, pe = {}, {}
        for arch, machine in [('i386-windows', 0x14c), ('x86_64-windows', 0x8664)]:
            data = bytearray(128)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 60, 64)
            data[64:68] = b'PE\0\0'
            struct.pack_into('<H', data, 68, machine)
            path = cross / 'pe' / arch / 'winegstreamer.dll'
            path.parent.mkdir(parents=True)
            path.write_bytes(data)
            hashes[arch] = module.sha(path)
            pe[arch + '/winegstreamer.dll'] = module.sha(path)
        module.write_json(cross / 'report.json', {'prx': {'modules': {'winegstreamer': entry}}, 'pe': pe,
                                               'wine_commit': 'a' * 40, 'patches': []})
        module.write_json(registration / 'dll-sha256.json', hashes)
        guid = '317df618-5e5a-468a-9f15-d827a9a08162'
        idl = cross / 'source/dlls/winegstreamer/winegstreamer_classes.idl'
        idl.parent.mkdir(parents=True)
        idl.write_text('uuid(' + guid + ')')
        base = 'WINE REGISTRY Version 2\n\n[Software\\\\Game]\n"keep"="yes"\n\n'
        (registration / 'system-before.reg').write_text(base)
        after = base
        for view, folder in [('', 'system32'), ('Wow6432Node\\\\', 'syswow64')]:
            after += ('[Software\\\\Classes\\\\' + view + 'CLSID\\\\{' + guid + '}\\\\InprocServer32]\n'
                      '@="C:\\\\windows\\\\' + folder + '\\\\winegstreamer.dll"\n"ThreadingModel"="Both"\n\n')
        (registration / 'system-after.reg').write_text(after)
        components = ['ffmpeg', 'libffi', 'pcre2', 'zlib', 'libiconv', 'glib', 'gstreamer',
                      'gst-plugins-base', 'gst-plugins-good', 'gst-plugins-bad', 'gst-libav']
        for component in components:
            archive = media / 'root/lib' / (component + '.a')
            archive.parent.mkdir(parents=True, exist_ok=True)
            archive.write_bytes(b'fixture static archive')
            license = cross / 'prx/licenses/media' / component / 'COPYING'
            license.parent.mkdir(parents=True, exist_ok=True)
            license.write_text('fixture license text')
            module.write_json(media / (component + '-manifest.json'), {
                'component': component, 'source': {'revision': 'pinned', 'sha256': 'b' * 64}, 'configure': [],
                'archives': {str(archive.relative_to(media / 'root')): module.sha(archive)},
                'licenses': {'licenses/' + component + '/COPYING': module.sha(license)}})
        module.write_json(cross / 'media-flags.json', {'libs': str(media / 'root/lib/ffmpeg.a')})
        module.write_json(cross / 'prx/media-adapters.json', {'schema': 'fixture'})
        template, runtime = root / 'template', root / 'runtime'
        template.mkdir()
        (template / 'system.reg').write_text(base)
        return cross, registration, media, template, runtime

    def test_media_stages_registry_license_sources_and_notice(self):
        from media_staging import stage_media
        with tempfile.TemporaryDirectory() as directory:
            cross, registration, media, template, runtime = self.fixture(Path(directory))
            files = []
            result = stage_media(runtime, template, cross, registration, media, files)
            self.assertFalse(result['playback_verified'])
            self.assertEqual(len(files), 2)
            self.assertIn('Wow6432Node', (template / 'system.reg').read_text())
            self.assertEqual(len(json.loads((runtime / 'share/wine/media-build/media-registry.json').read_text())['values']), 4)
            source = json.loads((runtime / 'share/wine/licenses/media/source-manifest.json').read_text())
            self.assertEqual(len(source['components']), 11)
            self.assertNotIn(directory, json.dumps(source))
            for component in source['components']:
                self.assertTrue((runtime / 'share/wine/licenses/media' / component['component'] / 'COPYING').is_file())
            self.assertIn('FFmpeg', (runtime / 'share/wine/THIRD_PARTY').read_text())

    def test_bad_license_missing_archive_and_gpl_reject_before_writes(self):
        from media_staging import stage_media
        for defect in ('license', 'archive', 'gpl', 'nonfree', 'source', 'unlisted'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as directory:
                cross, registration, media, template, runtime = self.fixture(Path(directory))
                manifest = media / 'ffmpeg-manifest.json'
                value = json.loads(manifest.read_text())
                if defect == 'license':
                    (cross / 'prx/licenses/media/ffmpeg/COPYING').write_text('changed')
                elif defect == 'archive':
                    (media / 'root/lib/ffmpeg.a').write_bytes(b'changed')
                elif defect in ('gpl', 'nonfree'):
                    value['configure'] = ['--enable-' + defect]
                elif defect == 'source':
                    value.pop('source')
                else:
                    value['archives'] = {}
                module.write_json(manifest, value)
                before = (template / 'system.reg').read_bytes()
                with self.assertRaises(ValueError):
                    stage_media(runtime, template, cross, registration, media, [])
                self.assertFalse(runtime.exists())
                self.assertEqual((template / 'system.reg').read_bytes(), before)

    def test_wrapped_registry_binary_is_normalized_before_staging(self):
        from media_staging import registry_values
        self.assertEqual(registry_values({'Software\\\\Classes\\\\media': '"FilterData"=hex:01,02,\\\n  03,04'}),
                         [{'key': 'Software\\Classes\\media', 'name': 'FilterData', 'data': 'hex:01,02,03,04'}])
        with self.assertRaisesRegex(ValueError, 'unsupported media registry'):
            registry_values({'Software\\\\Classes\\\\media': 'malformed'})

    def test_tls_licenses_required_and_shipped_with_native_module(self):
        from media_staging import stage_media
        with tempfile.TemporaryDirectory() as directory:
            cross, registration, media, template, runtime = self.fixture(Path(directory))
            (cross / 'prx/sce_module/libgnutls.prx').write_bytes(b'fixture TLS')
            with self.assertRaisesRegex(ValueError, 'TLS license inventory missing'):
                stage_media(runtime, template, cross, registration, media, [])
            self.assertFalse(runtime.exists())
            for component, names in [('gnutls', ('COPYING.LESSERv2', 'COPYING', 'AUTHORS', 'lib-inih-LICENSE.txt')),
                                     ('nettle', ('COPYING.LESSERv3', 'COPYINGv2', 'COPYINGv3', 'AUTHORS'))]:
                source = cross / 'prx/licenses' / component
                source.mkdir()
                for name in names:
                    (source / name).write_text('fixture license')
            stage_media(runtime, template, cross, registration, media, [])
            self.assertTrue((runtime / 'share/wine/licenses/nettle/COPYING.LESSERv3').is_file())
            self.assertTrue((runtime / 'share/wine/licenses/gnutls/COPYING.LESSERv2').is_file())
            self.assertIn('GnuTLS and Nettle', (runtime / 'share/wine/THIRD_PARTY').read_text())

    def test_legacy_overlay_cannot_retain_tls_without_notices(self):
        from media_staging import stage_media
        with tempfile.TemporaryDirectory() as directory:
            cross, registration, media, template, runtime = self.fixture(Path(directory))
            retained = runtime / 'lib/wine/x86_64-unix/libgnutls.prx'
            retained.parent.mkdir(parents=True)
            retained.write_bytes(b'old TLS')
            original = (template / 'system.reg').read_bytes()
            with self.assertRaisesRegex(ValueError, 'retained runtime'):
                stage_media(runtime, template, cross, registration, media, [])
            self.assertEqual((template / 'system.reg').read_bytes(), original)
            self.assertEqual([p for p in runtime.rglob('*') if p.is_file()], [retained])

    def run_stager(self, root, with_media):
        import os
        import subprocess
        if True:
            cross, registration, media, template, runtime = self.fixture(root)
            work = root / 'work'
            (work / 'kit-h').mkdir(parents=True)
            (work / 'kit-h/wine-ps5').symlink_to(cross, target_is_directory=True)
            install = work / 'kit-h/source/.deps/wine-host/install/usr'
            for folder in ('lib/wine/i386-windows', 'lib/wine/x86_64-windows', 'share/wine/nls'):
                (install / folder).mkdir(parents=True)
            build = root / 'build'
            entries = []
            for index in range(516):
                name = 'fixture-' + str(index) + '.dll'
                rel = 'lib/wine/x86_64-windows/' + name
                original = build / 'kit-f/inputs/runtime/wine' / rel
                original.parent.mkdir(parents=True, exist_ok=True)
                original.write_bytes(b'fixture PE')
                (install / rel).write_bytes(b'fixture PE')
                target = 'drive_c/windows/system32/' + name
                path = build / 'kit-a-6a77259/inputs/template/game' / target
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture PE')
                entries.append({'template_path': target, 'runtime_source': rel, 'after_sha256': module.sha(path)})
            a = build / 'kit-a-6a77259/inputs'
            module.write_json(a / 'template-substitutions.json', {'count': 516, 'files': entries})
            (a / 'template/game/drive_c/windows/syswow64').mkdir(parents=True)
            (a / 'template/game/system.reg').write_bytes((template / 'system.reg').read_bytes())
            for name in ('hidclass.sys', 'hidparse.sys', 'winebus.sys', 'winehid.sys', 'winexinput.sys'):
                (install / 'lib/wine/i386-windows' / name).write_bytes(b'fixture driver')
            mesa = root / 'mesa'
            for arch in ('i386-windows', 'x86_64-windows'):
                (mesa / arch).mkdir(parents=True)
                (mesa / arch / 'opengl32.dll').write_bytes(b'fixture Mesa')
            license_file = mesa / 'LICENSES/llvm/LICENSE.txt'
            license_file.parent.mkdir(parents=True)
            license_file.write_text('fixture compiler license')
            module.write_json(mesa / 'manifest.json', {'mesa_commit': 'fixture',
                              'license_sha256': {'LICENSES/llvm/LICENSE.txt': module.sha(license_file)}})
            kit = root / 'kit'
            (kit / ('inputs/titles/any-' + 'a' * 7)).mkdir(parents=True)
            env = {**os.environ, 'PLAYDEN_KIT_WORK': str(work), 'PLAYDEN_BUILD': str(build),
                   'PLAYDEN_KIT_DIR': str(kit), 'PLAYDEN_MESA_ARTIFACTS': str(mesa)}
            env.pop('PLAYDEN_MEDIA_WORK', None)
            env.pop('PLAYDEN_MEDIA_REGISTRATION', None)
            if with_media:
                env.update(PLAYDEN_MEDIA_REGISTRATION=str(registration), PLAYDEN_MEDIA_WORK=str(media))
            completed = subprocess.run([sys.executable, str(TOOLS / 'stage_kit_h.py'), 'a' * 40],
                                       env=env, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            return kit, license_file, (template / 'system.reg').read_text()

    def test_normal_stager_includes_media_without_overlay(self):
        with tempfile.TemporaryDirectory() as directory:
            kit, license_file, _ = self.run_stager(Path(directory), with_media=True)
            inventory = json.loads((kit / 'inputs/runtime-inventory.json').read_text())
            self.assertIn('wine/share/wine/licenses/media/source-manifest.json', inventory)
            self.assertTrue((kit / 'inputs/template/game/drive_c/windows/syswow64/winegstreamer.dll').is_file())
            self.assertIn('Wow6432Node', (kit / 'inputs/template/game/system.reg').read_text())
            self.assertEqual((kit / ('inputs/titles/any-' + 'a' * 7) / 'LICENSES/llvm/LICENSE.txt').read_bytes(),
                             license_file.read_bytes())

    def test_normal_stager_without_media_settings_leaves_media_out(self):
        with tempfile.TemporaryDirectory() as directory:
            kit, _, base = self.run_stager(Path(directory), with_media=False)
            inventory = json.loads((kit / 'inputs/runtime-inventory.json').read_text())
            self.assertFalse([k for k in inventory if 'winegstreamer' in k or '/licenses/media/' in k])
            self.assertFalse((kit / 'inputs/template/game/drive_c/windows/syswow64/winegstreamer.dll').exists())
            self.assertEqual((kit / 'inputs/template/game/system.reg').read_text(), base)
            self.assertIsNone(json.loads((kit / 'inputs/stage-record.json').read_text())['media'])

    def test_mesa_license_inventory_rejects_missing_changed_and_unsafe(self):
        from mesa_staging import validate_licenses
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for licenses in ({}, {'LICENSES/missing': '0' * 64}, {'../outside': '0' * 64}):
                module.write_json(root / 'manifest.json', {'license_sha256': licenses})
                with self.assertRaises(ValueError):
                    validate_licenses(root)
            source = root / 'LICENSES/COPYING'
            source.parent.mkdir()
            source.write_text('changed')
            module.write_json(root / 'manifest.json', {'license_sha256': {'LICENSES/COPYING': '0' * 64}})
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                validate_licenses(root)


class R10UpdateTests(unittest.TestCase):
    """r9 -> r10: the build is r9's plus the media patches, and every cross module replaces r9's."""
    BASE_PATCHES = ['0958-ntdll-honor-wine-cpu-topology-s-processor-count.patch', '0963-vkd3d-shader-hlsl.patch']

    def kit(self, root, extra_patch=False, new_module=False, skip_ntdll=False):
        cross, registration, media, _, _ = NormalMediaStagingTests().fixture(root)
        previous, here = root / 'r9', root / 'r10'
        runtime, template = here / 'inputs/runtime/wine', here / 'inputs/template/game'
        files = []
        modules = [('x86_64-unix', 'ntdll.prx', None, True), ('x86_64-unix', 'libgnutls.prx', None, False),
                   ('i386-windows', 'rtworkq.dll', 'syswow64', True), ('x86_64-windows', 'rtworkq.dll', 'system32', True),
                   ('x86_64-windows', 'wined3d.dll', 'system32', False)]
        for arch, name, folder, rebuilt in modules:
            old = runtime / 'lib/wine' / arch / name
            old.parent.mkdir(parents=True, exist_ok=True)
            old.write_bytes(b'r9 ' + name.encode())
            if folder:
                target = template / 'drive_c/windows' / folder / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(old.read_bytes())
                files.append({'template_path': str(target.relative_to(template)), 'runtime_source': f'lib/wine/{arch}/{name}',
                              'before_sha256': module.sha(target), 'after_sha256': module.sha(target)})
            if name == 'ntdll.prx' and skip_ntdll:
                continue
            new = cross / ('prx/sce_module' if arch == 'x86_64-unix' else 'pe/' + arch) / name
            new.parent.mkdir(parents=True, exist_ok=True)
            new.write_bytes(b'r10 ' + name.encode() if rebuilt else old.read_bytes())
        if new_module:
            (cross / 'pe/x86_64-windows/mfplat.dll').write_bytes(b'r10 mfplat')
        for component, names in [('gnutls', ('COPYING.LESSERv2', 'COPYING', 'AUTHORS', 'lib-inih-LICENSE.txt')),
                                 ('nettle', ('COPYING.LESSERv3', 'COPYINGv2', 'COPYINGv3', 'AUTHORS'))]:
            for name in names:
                path = cross / 'prx/licenses' / component / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture license')
        report = json.loads((cross / 'report.json').read_text())
        report['patches'] = self.BASE_PATCHES + module.WINE_PATCHES + (['0999-extra.patch'] if extra_patch else [])
        module.write_json(cross / 'report.json', report)
        module.write_json(root / 'r9-report.json', {'wine_commit': report['wine_commit'], 'patches': self.BASE_PATCHES})
        (template / 'system.reg').write_text('WINE REGISTRY Version 2\n\n[Software\\\\Game]\n"keep"="yes"\n\n')
        previous.mkdir()
        module.write_json(here / 'kit-spec.json', {'runtime': {'artifact_revision': 'b9b0aa5-kit-h-r9'},
                                                   'sources': {'wine': {'revision': 'b' * 40}}})
        module.write_json(here / 'inputs/template-substitutions.json', {'count': len(files), 'files': files})
        module.write_json(here / 'inputs/stage-record.json', {'artifact_revision': 'b9b0aa5-kit-h-r9',
                                                              'cross_pe_modules': ['x86_64-windows/wined3d.dll']})
        return here, previous, cross, registration, media, root / 'r9-report.json'

    def test_stages_media_and_the_rebuilt_modules(self):
        with tempfile.TemporaryDirectory() as directory:
            here, previous, cross, registration, media, base = self.kit(Path(directory))
            result = module.stage(here, previous, cross, registration, media, base, 'c' * 40, 'r10-dev-20261015.1')
            self.assertEqual(sorted(result['changed']), ['lib/wine/i386-windows/rtworkq.dll',
                             'lib/wine/x86_64-unix/ntdll.prx', 'lib/wine/x86_64-windows/rtworkq.dll'])
            self.assertEqual(sorted(result['template_changed']), ['drive_c/windows/system32/rtworkq.dll',
                                                                  'drive_c/windows/syswow64/rtworkq.dll'])
            self.assertEqual(result['wine_patches'][-1], '0964-ntdll-ps5-retire-thread-alert-kqueues')
            runtime = here / 'inputs/runtime/wine/lib/wine'
            self.assertEqual((runtime / 'x86_64-unix/ntdll.prx').read_bytes(), b'r10 ntdll.prx')
            self.assertEqual((runtime / 'x86_64-windows/wined3d.dll').read_bytes(), b'r9 wined3d.dll')
            self.assertTrue((runtime / 'x86_64-unix/winegstreamer.prx').is_file())
            self.assertEqual((here / 'inputs/template/game/drive_c/windows/syswow64/rtworkq.dll').read_bytes(),
                             b'r10 rtworkq.dll')
            spec = json.loads((here / 'kit-spec.json').read_text())
            self.assertEqual(spec['runtime']['artifact_revision'], 'ccccccc-kit-h-r10-dev-20261015.1')
            self.assertFalse(spec['sources']['media']['playback_verified'])
            record = json.loads((here / 'inputs/stage-record.json').read_text())
            self.assertEqual(record['r10']['base_runtime'], 'b9b0aa5-kit-h-r9')
            self.assertIn('i386-windows/winegstreamer.dll', record['cross_pe_modules'])
            inventory = json.loads((here / 'inputs/runtime-inventory.json').read_text())
            self.assertEqual(inventory['wine/lib/wine/x86_64-unix/ntdll.prx'], module.sha(runtime / 'x86_64-unix/ntdll.prx'))

    def test_other_patches_new_modules_or_missing_modules_stop_before_writes(self):
        for case, error in (({'extra_patch': True}, 'patches differ.*0999'),
                            ({'new_module': True}, 'r9 does not have.*mfplat'),
                            ({'skip_ntdll': True}, 'missing from the build.*ntdll')):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                here, previous, cross, registration, media, base = self.kit(Path(directory), **case)
                before = {p: p.read_bytes() for p in here.rglob('*') if p.is_file()}
                with self.assertRaisesRegex(ValueError, error):
                    module.stage(here, previous, cross, registration, media, base, 'c' * 40)
                self.assertEqual({p: p.read_bytes() for p in here.rglob('*') if p.is_file()}, before)

    def test_rejects_a_base_other_than_r9_and_bad_names(self):
        with tempfile.TemporaryDirectory() as directory:
            here, previous, cross, registration, media, base = self.kit(Path(directory))
            with self.assertRaisesRegex(ValueError, 'name is r10'):
                module.stage(here, previous, cross, registration, media, base, 'c' * 40, 'r11')
            module.write_json(here / 'kit-spec.json', {'runtime': {'artifact_revision': 'x-kit-h-r8'}})
            with self.assertRaisesRegex(ValueError, 'starts from r9'):
                module.stage(here, previous, cross, registration, media, base, 'c' * 40)


if __name__ == "__main__":
    unittest.main()
