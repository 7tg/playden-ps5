#!/usr/bin/env python3
"""Supplemental proofs use only the unchanged shared catalog/scenarios."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'results/ui-comparison/homebrew-ui'
CASES = [
    ('multilingual-arabic', 'default', 'action details:900008\n', [(1920,1080),(3840,2160)]),
    ('details-focused-favorite', 'default', 'action details:900012\naction down\n', [(1920,1080)]),
    ('library-refresh', 'default', 'action route:library\naction inject:library-refresh\n', [(1920,1080)]),
    ('signed-out-home', 'signed-out', 'action focus:startup-skip\naction cross\n', [(1920,1080)]),
    ('delayed-art-before', 'delayed-art', 'action route:library\naction select:900007\n', [(1920,1080)]),
    ('delayed-art-after', 'delayed-art', 'action route:library\naction select:900007\ntick 1\n', [(1920,1080)]),
    ('stress-focused', 'stress', 'action select:100500\n', [(1920,1080)]),
    ('stress-traversal', 'stress', ''.join('action r2\ntick 1\n' for _ in range(20)), [(3840,2160)]),
]
with tempfile.TemporaryDirectory(prefix='playden-hui-proof-') as temp:
    for case,scenario,replay,sizes in CASES:
        replay_path = BASE/'replays'/(case+'.txt')
        replay_path.write_text(replay)
        for width,height in sizes:
            name=f'{case}-{width}x{height}'
            png=BASE/'screenshots'/(name+'.png')
            snapshot=BASE/'assertions'/(name+'.json')
            command=[str(ROOT/'build/homebrew-host/playden-homebrew'),'--scenario',scenario,'--profile',str(Path(temp)/(name+'.json')),'--replay',str(replay_path),'--capture',str(png),'--snapshot',str(snapshot),'--width',str(width),'--height',str(height)]
            result=subprocess.run(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (BASE/'assertions'/(name+'.log')).write_text(result.stdout)
            if result.returncode: raise RuntimeError(result.stdout)
            state=json.loads(snapshot.read_text());render=json.loads(Path(str(png)+'.render.json').read_text())
            assert (render['native_width'],render['native_height'])==(width,height)
            assert render['artwork_texture_count']==9 and render['grid_drawn_cell_count']<=24
            if case.startswith('stress'):
                assert state['visible_count']==1000 and state['materialized_row_count']<=24
                assert render['grid_focused_game_id']==state['selected_game_id']
            if case=='details-focused-favorite':
                assert state['focus']==render['focused_control_key']=='details-favorite'
            if case=='signed-out-home':
                assert state['account']=='signed-out' and state['visible_count']==0
            if case=='library-refresh': assert state['library_loading']
            if case=='multilingual-arabic': assert state['selected_game_id']==900008 and state['clock']==1760000000
            print('PASS',name,state['focus'],render['focused_control_key'])
