"""Keep the Mesa artifact's declared licenses with its staged DLLs."""
import hashlib
import json
import shutil
from pathlib import Path


def validate_licenses(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    licenses = manifest.get('license_sha256')
    if not isinstance(licenses, dict) or not licenses:
        raise ValueError('Mesa license inventory missing')
    for name, expected in licenses.items():
        parts = Path(name).parts
        if len(parts) < 2 or parts[0] != 'LICENSES' or '..' in parts:
            raise ValueError('unsafe Mesa license path: ' + name)
        source = directory
        for part in parts:
            source /= part
            if source.is_symlink():
                raise ValueError('symlink Mesa license: ' + name)
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise ValueError('Mesa license hash mismatch: ' + name)
    return manifest


def stage_licenses(directory, shell, manifest):
    for name in manifest['license_sha256']:
        target = shell / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(directory / name, target)
        target.chmod(0o644)
