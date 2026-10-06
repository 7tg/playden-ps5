#!/usr/bin/env python3
"""Upload a packaged kit to the console over FTP with exact modes, then verify.

The kit is written into a temporary sibling directory, every file is read back
and hashed, file and directory modes are compared with manifest.json, and only
then is the directory renamed to its final name. An existing final directory is
never replaced. Development tooling; it does not install or register anything.
"""
import argparse
import ftplib
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import sys


def listing(ftp, directory):
    """name -> (mode string, is_dir, size) for one remote directory."""
    entries = {}
    lines = []
    ftp.retrlines("LIST " + directory, lines.append)
    for line in lines:
        parts = line.split(None, 8)
        if len(parts) < 9 or parts[8] in (".", ".."):
            continue
        entries[parts[8]] = (parts[0], parts[0].startswith("d"), int(parts[4]))
    return entries


def mode_string(mode, directory):
    bits = "".join(flag if mode & (1 << (8 - index)) else "-"
                   for index, flag in enumerate("rwxrwxrwx"))
    return ("d" if directory else "-") + bits


def remote_sha256(ftp, path):
    digest = hashlib.sha256()
    ftp.retrbinary("RETR " + path, digest.update, 1 << 20)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kit", type=Path)
    parser.add_argument("--host", default="10.0.1.68")
    parser.add_argument("--port", type=int, default=2120)
    parser.add_argument("--remote-parent", default="/data/playden/kits")
    args = parser.parse_args()
    manifest = json.loads((args.kit / "manifest.json").read_text())
    kit_id = manifest["kit_id"]
    final = f"{args.remote_parent}/{kit_id}"
    staging = f"{args.remote_parent}/.{kit_id}.upload"
    ftp = ftplib.FTP()
    ftp.connect(args.host, args.port, timeout=60)
    ftp.login()
    parent = listing(ftp, args.remote_parent)
    if kit_id in parent:
        print(f"refusing: {final} already exists", file=sys.stderr)
        return 1
    if f".{kit_id}.upload" in parent:
        print(f"refusing: stale {staging} exists; inspect it first", file=sys.stderr)
        return 1
    files = {entry["path"]: entry for entry in manifest["files"]}
    files["manifest.json"] = {"path": "manifest.json", "mode": "0644",
                              "size": (args.kit / "manifest.json").stat().st_size,
                              "sha256": hashlib.sha256((args.kit / "manifest.json").read_bytes()).hexdigest()}
    directories = sorted({str(parent) for path in files for parent in PurePosixPath(path).parents
                          if str(parent) != "."}, key=lambda value: value.count("/"))
    ftp.mkd(staging)
    ftp.sendcmd(f"SITE CHMOD 755 {staging}")
    for directory in directories:
        ftp.mkd(f"{staging}/{directory}")
        ftp.sendcmd(f"SITE CHMOD 755 {staging}/{directory}")
    for index, (path, entry) in enumerate(sorted(files.items()), 1):
        with (args.kit / path).open("rb") as stream:
            ftp.storbinary(f"STOR {staging}/{path}", stream, 1 << 20)
        ftp.sendcmd(f"SITE CHMOD {entry['mode'][-3:]} {staging}/{path}")
        if index % 250 == 0:
            print(f"uploaded {index}/{len(files)}", flush=True)
    # Independent read-back: bytes, sizes and modes as the console reports them.
    problems = []
    by_directory = {}
    for path in files:
        by_directory.setdefault(str(PurePosixPath(path).parent), []).append(path)
    for directory, paths in by_directory.items():
        remote_directory = staging if directory == "." else f"{staging}/{directory}"
        entries = listing(ftp, remote_directory)
        for path in paths:
            name = PurePosixPath(path).name
            entry = files[path]
            observed = entries.get(name)
            expected_mode = mode_string(int(entry["mode"], 8), False)
            if not observed or observed[0] != expected_mode or observed[2] != entry["size"]:
                problems.append(f"{path}: listed {observed} expected {expected_mode} size {entry['size']}")
            elif remote_sha256(ftp, f"{remote_directory}/{name}") != entry["sha256"]:
                problems.append(f"{path}: sha256 mismatch")
    for directory in directories:
        head, _, name = (f"{staging}/{directory}").rpartition("/")
        observed = listing(ftp, head).get(name)
        if not observed or observed[0] != "drwxr-xr-x":
            problems.append(f"{directory}/: listed {observed}")
    if problems:
        print("verification failed; staging kept for inspection:", file=sys.stderr)
        for problem in problems[:50]:
            print("  " + problem, file=sys.stderr)
        return 2
    ftp.rename(staging, final)
    print(f"verified {len(files)} files and {len(directories)} directories; installed {final}")
    ftp.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
