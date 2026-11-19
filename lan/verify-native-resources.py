#!/usr/bin/env python3
"""Bounded map-load checks using the real extracted data (not a playthrough)."""
from pathlib import Path
import os
import hashlib
import json
import subprocess
import time

root = Path(os.environ.get('WC3_LAN_ROOT', Path.home() / '.local/share/wc3-lan')).resolve()
build = root / 'resource-builds/20261114'
engine = root / 'evaluation/src/build/bin/openwarcraft3'
source = root / 'evaluation/src'
data = build / 'gamedata'
maps = ['maps/frozenthrone/(2)banditridge.w3x', 'campaign/classic/roc/human01.w3m']
checks = []
for index, map_path in enumerate(maps):
    command = [str(engine), '-data', str(data), '+dedicated', '1', '+game_port', '27961',
               '+fs_expansion', '1', '+com_frame_limit', '120', '+com_fast_forward', '1', '+map', map_path]
    start = time.monotonic()
    log = build / ('native-map-%d.log' % index)
    print('Checking', map_path, flush=True)
    timed_out = False
    with log.open('w') as out:
        p = subprocess.Popen(command, cwd=source, stdout=out, stderr=subprocess.STDOUT,
                             stdin=subprocess.DEVNULL)
        try:
            p.wait(timeout=180)
        except subprocess.TimeoutExpired:
            timed_out = True
            p.terminate()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
    text = log.read_text(errors='replace')
    passed = (p.returncode == 0 and not timed_out and 'Server initialized.' in text
              and 'OpenWarcraft3 initialized.' in text and 'JASS runtime error' not in text)
    record = dict(map=map_path, passed=passed, exitCode=p.returncode, timedOut=timed_out,
                  seconds=round(time.monotonic() - start, 3),
                  serverInitialized='Server initialized.' in text,
                  jassRuntimeErrors=text.count('JASS runtime error'), log=log.name)
    checks.append(record)
    print(json.dumps(record), flush=True)
result = dict(engineSha256=hashlib.sha256(engine.read_bytes()).hexdigest(),
              actualResourceMapBootChecks=checks, allMapBootChecksPassed=all(c['passed'] for c in checks),
              completeGameplayVerified=False, multiplayerVerified=False)
(build / 'native-resources-verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result), flush=True)
