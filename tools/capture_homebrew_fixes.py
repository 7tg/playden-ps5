#!/usr/bin/env python3
"""Native framebuffer proofs for the post-selection UI fixes (requires Pillow/ffmpeg)."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'results/ui-comparison/homebrew-ui/fixes-2026-10-04'
CASES = {
    'quit-wrap': 'action route:settings\naction focus:settings-quit\naction cross\n',
    'cjk-library': 'action route:library\naction select:900007\n',
    'greek-details': 'action details:900003\n',
    'home-tabs': 'action route:home\n',
}

def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    executable = str(ROOT / 'build/homebrew-host/playden-homebrew')
    with tempfile.TemporaryDirectory(prefix='playden-homebrew-fixes-') as tmp:
        for case, replay in CASES.items():
            replay_path = OUT / (case + '.txt')
            replay_path.write_text(replay)
            for width, height in [(1920, 1080), (3840, 2160)]:
                name = f'{case}-{width}x{height}'
                png = OUT / (name + '.png')
                run([executable, '--headless', '--scenario', 'default', '--profile',
                     str(Path(tmp) / (name + '.json')), '--replay', str(replay_path),
                     '--capture', str(png), '--snapshot', str(OUT / (name + '.json')),
                     '--width', str(width), '--height', str(height)])
                image = Image.open(png).convert('RGB')
                assert image.size == (width, height)
                scale = width // 1920
                if case == 'quit-wrap':
                    # Amber heading must never paint beyond the right panel edge.
                    pixels = image.load()
                    assert not any(r > 100 and g > 80 and b < g * .7
                                   for y in range(275 * scale, 410 * scale)
                                   for x in range(1370 * scale, 1600 * scale)
                                   for r, g, b in [pixels[x, y]])
                elif case == 'home-tabs':
                    r, g, b = image.getpixel((100 * scale, 80 * scale))
                    assert r > 200 and r > g * 1.5 and b < 100, (r, g, b)
                print('PASS', name)
        replay_path = OUT / 'focus-motion.txt'
        replay_path.write_text('action route:home\nframe 30\naction right\nframe 30\n'
                               'action right\nframe 3\naction right\nframe 60\n')
        frames = Path(tmp) / 'frames'
        run([executable, '--scenario', 'default', '--profile', str(Path(tmp) / 'motion.json'),
             '--record-motion', '--record-dir', str(frames), '--replay', str(replay_path)])
        states = [json.loads(line) for line in (frames / 'states.jsonl').read_text().splitlines()]
        assert len(states) == 128, len(states)
        assert all(item['state']['clock'] == 1760000000 for item in states)
        assert (frames / 'frame-0032.png').read_bytes() != (frames / 'frame-0035.png').read_bytes()
        run(['ffmpeg', '-y', '-framerate', '60', '-i', str(frames / 'frame-%04d.png'),
             '-c:v', 'libx264', '-crf', '18', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
             str(OUT / 'focus-motion.mp4')])
        shutil.copyfile(frames / 'states.jsonl', OUT / 'focus-motion.jsonl')
        print('PASS native focus motion: 128 frames at 60 Hz; fixture clock unchanged')

if __name__ == '__main__':
    main()
