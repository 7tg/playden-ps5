#!/usr/bin/env python3
"""Render the common capture definitions through the runnable Hui candidate."""
import argparse
import json
import pathlib
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/ui-comparison/homebrew-ui"


def recipe(capture):
    surface = capture["surface"]
    game = capture["game_id"]
    lines = []
    def action(value):
        lines.append("action " + value)
    def focus_activate(key):
        action("focus:" + key)
        action("cross")
    if surface in ("home", "library", "downloads", "settings"):
        action("route:" + surface)
        if game is not None:
            action("select:" + str(game))
    elif surface == "qr":
        pass
    elif surface in ("filters", "keyboard"):
        action("route:library")
        action("select:" + str(game))
        action("triangle" if surface == "filters" else "search")
    elif surface == "install":
        # The shared insufficient-space scenario opens the normal offer.
        pass
    elif surface == "storage":
        action("route:settings")
        action("select:" + str(game))
        focus_activate("settings-storage")
    else:
        action("details:" + str(game))
        if surface == "details":
            pass
        elif surface in ("context", "collection-editor", "uninstall", "logs"):
            action("options")
            if surface != "context":
                focus_activate({"collection-editor": "collection", "uninstall": "uninstall", "logs": "logs"}[surface])
                if surface == "collection-editor":
                    focus_activate("collection-new")
        elif surface in ("game-settings", "setting-picker", "profile"):
            focus_activate("details-settings")
            if surface != "game-settings":
                focus_activate("resolution-picker" if surface == "setting-picker" else "profile-picker")
        elif surface in ("launch", "session-controls"):
            focus_activate("details-simulate-launch")
            if surface == "session-controls":
                action("inject:session-running")
        else:
            raise ValueError("Unmapped capture surface: " + surface)
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, default=ROOT / "build/homebrew-host/playden-homebrew")
    parser.add_argument("--only", help="Comma-separated common capture IDs")
    args = parser.parse_args()
    captures = json.loads((ROOT / "fixtures/ui-comparison/scenarios.json").read_text())["captures"]
    if args.only:
        ids = set(args.only.split(","))
        captures = [c for c in captures if c["id"] in ids]
    screenshots = RESULTS / "screenshots"
    replays = RESULTS / "replays"
    snapshots = RESULTS / "assertions"
    for path in (screenshots, replays, snapshots):
        path.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="playden-hui-captures-") as temp:
        for capture in captures:
            replay = replays / (capture["id"] + ".txt")
            replay.write_text(recipe(capture))
            for width, height in capture["resolutions"]:
                name = f"{capture['id']}-{width}x{height}"
                snapshot = snapshots / (name + ".json")
                command = [str(args.executable), "--headless", "--scenario", capture["scenario"], "--profile", str(pathlib.Path(temp) / (name + ".json")), "--replay", str(replay), "--capture", str(screenshots / (name + ".png")), "--snapshot", str(snapshot), "--width", str(width), "--height", str(height)]
                completed = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                (snapshots / (name + ".log")).write_text(completed.stdout)
                if completed.returncode:
                    raise RuntimeError(f"Capture failed: {name}\n{completed.stdout}")
                state = json.loads(snapshot.read_text())
                assert state["simulation"] is True
                assert state["clock"] == capture["clock"], (name, state)
                expected_surface = {"setting-picker": "choice-picker", "profile": "profile-picker", "uninstall": "confirmation", "collection-editor": "keyboard", "launch": "session", "session-controls": "session"}.get(capture["surface"], capture["surface"])
                assert state["surface"] == expected_surface, (name, expected_surface, state)
                if capture["game_id"] is not None:
                    assert state["selected_game_id"] == capture["game_id"], (name, state)
                png = (screenshots / (name + ".png")).read_bytes()
                assert png[:8] == b"\x89PNG\r\n\x1a\n"
                assert struct.unpack(">II", png[16:24]) == (width, height), name
                if capture["id"] == "downloads-active":
                    assert state["focus"] == "job-job-1", (name, state)
                    assert state["jobs"][0]["completed"] == 461708984
                    assert state["jobs"][0]["total"] == 1073741824
                if capture["id"] == "uninstall":
                    assert state["focus"] == "cancel", (name, state)
                print("PASS", name, state["route"], state["surface"], state["focus"])


if __name__ == "__main__":
    main()
