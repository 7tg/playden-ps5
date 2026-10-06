#!/usr/bin/env python3
"""Build a local diagnostic from independently pinned production inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

REVISION = "1a8a0d5c0b056487aa0b35316cc0eef3d43dc684"
FOUNDATION = "9c0b994a048521af6fb84c73ded364504fe250e9"
PROVIDER = "f14d5e3ecb31951e3cf5bfdc41972181a9be1d96ed0dc7cb3aeebb8e4aab844a"
SDK_ARCHIVE = "8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da"
SOURCES = [
    "native/pw_title_publication_native.c",
    "native/pw_title_publication_ps5_lock.c",
    "src/pw_title_publication.c", "src/pw_title_launch.c",
    "src/pw_game_profile.c", "src/pw_app_profile.c",
    "src/pw_profile_catalog.c", "src/pw_wine_launch.c",
    "src/pw_publication_sha256.c",
    "third_party/mbedtls-publication/library/sha256.c",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(directory, *args):
    return subprocess.check_output(["git", "-C", str(directory), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--foundation", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path("third_party/prospero-win"))
    parser.add_argument("--output", type=Path, default=Path("build/publication-acceptance"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = args.source.resolve()
    foundation = args.foundation.resolve()
    sdk = foundation / ".deps/native/ps5-payload-sdk"
    compiler = sdk / "bin/prospero-clang"
    if git(source, "rev-parse", "HEAD").decode().strip() != REVISION:
        raise ValueError("Diagnostic requires the exact reviewed guard revision")
    if git(source, "status", "--porcelain"):
        raise ValueError("Pinned production checkout must be clean")
    if git(foundation, "rev-parse", "HEAD").decode().strip() != FOUNDATION:
        raise ValueError("Foundation revision does not match the reviewed SDK")
    archive = sdk.parent / "ps5-payload-sdk.zip"
    if digest(archive) != SDK_ARCHIVE:
        raise ValueError("SDK archive is not the pinned v0.42 artifact")
    # macOS replaces Linux host-tool symlinks, but target headers/libraries must
    # still match the independently pinned release archive byte for byte.
    with zipfile.ZipFile(archive) as frozen_sdk:
        for member in frozen_sdk.namelist():
            prefix = "ps5-payload-sdk/"
            if not member.startswith(prefix):
                continue
            relative = member[len(prefix):]
            if member.endswith("/") or not relative.startswith(("target/include/", "target/lib/")):
                continue
            if (sdk / relative).read_bytes() != frozen_sdk.read(member):
                raise ValueError("SDK target input differs from pinned archive: " + relative)
    provider = source / "third_party/mbedtls-publication/provenance.json"
    if digest(provider) != PROVIDER:
        raise ValueError("Independent publication provider pin does not match")
    # Check every tracked production byte, including transitively included headers.
    inputs = {}
    for relative in git(source, "ls-files", "-z").decode().split("\0"):
        if not relative:
            continue
        path = source / relative
        if not path.is_file() or path.is_symlink():
            raise ValueError("Pinned source contains a missing or symlinked input")
        frozen = git(source, "show", REVISION + ":" + relative)
        if path.read_bytes() != frozen:
            raise ValueError("Production input differs from its pinned commit: " + relative)
        inputs["prospero/" + relative] = digest(path)
    authored = [root / "probes/publication-acceptance/main.c",
                root / "src/platform/ps5/file_shims.c"]
    for path in authored:
        inputs[str(path.relative_to(root))] = digest(path)
    output = args.output.resolve()
    # A failed rebuild must never leave an older valid pair at this build path.
    # Choose a new output path for each frozen diagnostic build.
    output.mkdir(parents=True, exist_ok=False)
    elf = output / "publication-acceptance.elf"
    command = [
        str(compiler), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-D_DEFAULT_SOURCE", '-DMBEDTLS_CONFIG_FILE="pw_publication_config.h"',
        "-I" + str(source / "native"), "-I" + str(source / "src"),
        "-I" + str(source / "third_party/mbedtls-publication"),
        "-I" + str(source / "third_party/mbedtls-publication/include"),
        *map(str, authored), *[str(source / relative) for relative in SOURCES],
        "-lkernel", "-o", str(elf),
    ]
    environment = {"PATH": str(sdk / "bin") + ":/usr/bin:/bin:/usr/sbin:/sbin",
                   "PS5_PAYLOAD_SDK": str(sdk), "LC_ALL": "C",
                   "LLVM_CONFIG": "/opt/homebrew/opt/llvm@18/bin/llvm-config"}
    subprocess.run(command, check=True, env=environment)
    for relative, expected in inputs.items():
        path = source / relative[len("prospero/"):] if relative.startswith("prospero/") else root / relative
        if digest(path) != expected:
            raise ValueError("Source input changed during diagnostic build: " + relative)
    # Freeze the sysroot too: a foundation revision alone does not bind its .deps.
    sdk_inputs = {}
    for tree in ("bin", "target/include", "target/lib"):
        for path in sorted((sdk / tree).rglob("*")):
            if path.is_file():
                sdk_inputs[str(path.relative_to(sdk))] = digest(path)
    llvm_bin = Path(subprocess.check_output([environment["LLVM_CONFIG"], "--bindir"], env=environment).decode().strip())
    host_tools = {str(path.resolve()): digest(path.resolve()) for path in
                  (Path(environment["LLVM_CONFIG"]), llvm_bin / "clang", llvm_bin / "ld.lld")}
    manifest = {
        "schema": "playden-publication-acceptance/1",
        "prospero_revision": REVISION, "foundation_revision": FOUNDATION,
        "publication_provider_sha256": PROVIDER,
        "sdk_archive_sha256": SDK_ARCHIVE,
        "elf": elf.name, "elf_sha256": digest(elf),
        "inputs": inputs, "sdk_inputs": sdk_inputs,
        "sdk_inputs_sha256": hashlib.sha256(
            json.dumps(sdk_inputs, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
        "command": command, "build_environment": environment, "host_tools": host_tools,
        "compiler_version": subprocess.check_output([str(compiler), "--version"], env=environment).decode(),
        "acceptance_scope": "standalone-payload; no granted-wrapper lifetime claim",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print("Built diagnostic:", elf)
    print("SHA-256:", manifest["elf_sha256"])


if __name__ == "__main__":
    main()
