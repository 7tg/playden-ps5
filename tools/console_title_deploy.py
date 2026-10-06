#!/usr/bin/env python3
"""Deploy or restore the Playden FAKE77031 package files over FTP.

deploy: compares every packaged file with the console, backs up each differing
console file locally (bytes, mode, SHA-256), then replaces it through a staged
upload, exact mode, read-back hash and rename. restore: puts a backup back the
same way. Game downloads, logs and user data under the title are never touched.
"""
import argparse
import ftplib
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import sys

TITLE = "/data/homebrew/FAKE77031"
SKIP = {".packaged"}
SKIP_DIRS = {"logs"}


def listing(ftp, directory):
    entries, lines = {}, []
    try:
        ftp.retrlines("LIST " + directory, lines.append)
    except ftplib.error_temp:
        return None
    for line in lines:
        parts = line.split(None, 8)
        if len(parts) >= 9 and parts[8] not in (".", ".."):
            entries[parts[8]] = parts[0]
    return entries


def octal_mode(listed):
    bits = listed[1:10]
    return "".join(str(sum(4 >> i for i, flag in enumerate(bits[group:group + 3]) if flag != "-"))
                   for group in (0, 3, 6))


def fetch(ftp, path):
    buffer = io.BytesIO()
    ftp.retrbinary("RETR " + path, buffer.write, 1 << 20)
    return buffer.getvalue()


def replace(ftp, path, data, mode):
    head, _, name = path.rpartition("/")
    stage, old = f"{head}/.{name}.stage", f"{head}/.{name}.old"
    existing = listing(ftp, head) or {}
    if f".{name}.stage" in existing or f".{name}.old" in existing:
        raise RuntimeError(f"stale staging beside {path}; inspect it first")
    ftp.storbinary("STOR " + stage, io.BytesIO(data), 1 << 20)
    ftp.sendcmd(f"SITE CHMOD {mode} {stage}")
    if fetch(ftp, stage) != data or octal_mode((listing(ftp, head) or {})[f".{name}.stage"]) != mode:
        raise RuntimeError(f"staged {path} did not verify")
    if name in existing:
        ftp.rename(path, old)
    ftp.rename(stage, path)
    entries = listing(ftp, head) or {}
    if fetch(ftp, path) != data or octal_mode(entries[name]) != mode:
        raise RuntimeError(f"installed {path} did not verify; previous copy kept as {old}")
    if name in existing:
        ftp.delete(old)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    deploy = commands.add_parser("deploy")
    deploy.add_argument("package", type=Path)
    deploy.add_argument("--backup", type=Path, required=True)
    restore = commands.add_parser("restore")
    restore.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--host", default="10.0.1.68")
    parser.add_argument("--port", type=int, default=2120)
    args = parser.parse_args()
    ftp = ftplib.FTP()
    ftp.connect(args.host, args.port, timeout=60)
    ftp.login()
    if args.command == "restore":
        record = json.loads((args.backup / "backup.json").read_text())
        for path, entry in record["files"].items():
            remote = f"{TITLE}/{path}"
            if entry["sha256"] is None:
                print(f"{path}: was absent before deploy; left in place (remove manually if needed)")
                continue
            data = (args.backup / "files" / path).read_bytes()
            assert hashlib.sha256(data).hexdigest() == entry["sha256"]
            replace(ftp, remote, data, entry["mode"])
            print(f"restored {path} {entry['sha256'][:12]} {entry['mode']}")
        return 0
    if (args.backup / "backup.json").exists():
        print("refusing: backup directory already holds a backup", file=sys.stderr)
        return 1
    changes, record = [], {"title": TITLE, "files": {}}
    for local in sorted(path for path in args.package.rglob("*") if path.is_file()):
        relative = local.relative_to(args.package).as_posix()
        if local.name in SKIP or PurePosixPath(relative).parts[0] in SKIP_DIRS:
            continue
        data = local.read_bytes()
        mode = f"{local.stat().st_mode & 0o777:o}"
        remote = f"{TITLE}/{relative}"
        head, _, name = remote.rpartition("/")
        entries = listing(ftp, head)
        current = fetch(ftp, remote) if entries and name in entries else None
        current_mode = octal_mode(entries[name]) if current is not None else None
        if current == data and current_mode == mode:
            continue
        changes.append((relative, data, mode))
        record["files"][relative] = {
            "sha256": hashlib.sha256(current).hexdigest() if current is not None else None,
            "mode": current_mode, "deployed_sha256": hashlib.sha256(data).hexdigest(), "deployed_mode": mode}
        if current is not None:
            target = args.backup / "files" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(current)
    args.backup.mkdir(parents=True, exist_ok=True)
    (args.backup / "backup.json").write_text(json.dumps(record, indent=2) + "\n")
    for relative, data, mode in changes:
        head = f"{TITLE}/{PurePosixPath(relative).parent}"
        if listing(ftp, head) is None:
            raise RuntimeError(f"remote directory {head} is missing; not creating package directories")
        replace(ftp, f"{TITLE}/{relative}", data, mode)
        print(f"deployed {relative} {hashlib.sha256(data).hexdigest()[:12]} {mode}")
    print(f"{len(changes)} files changed; backup in {args.backup}")
    ftp.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
