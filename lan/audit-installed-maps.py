#!/usr/bin/env python3
"""Bounded initialization audit of the supplied maps, separate from unit fixtures."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import argparse, json, os, re, subprocess, time

parser = argparse.ArgumentParser()
retry = parser.add_mutually_exclusive_group()
retry.add_argument('--retry-crashes', type=Path)
retry.add_argument('--retry-failures', type=Path, help='Retry maps marked failed in a prior audit')
parser.add_argument('--error-match', help='Restrict --retry-failures to an error regular expression')
parser.add_argument('--list-only', action='store_true', help='Print selected maps without launching the engine')
parser.add_argument('--output', default='map-audit')
parser.add_argument('--engine', type=Path)
parser.add_argument('--catalog', type=Path, help='Original JSON/JSONL catalog, including currently rejected maps')
args = parser.parse_args()
if not re.fullmatch(r'[a-zA-Z0-9._-]+', args.output): parser.error('Invalid output name')
if args.error_match and not args.retry_failures: parser.error('--error-match requires --retry-failures')
try: error_match = re.compile(args.error_match) if args.error_match else None
except re.error as error: parser.error(str(error))

root = Path(os.environ.get('WC3_LAN_ROOT', Path.home() / '.local/share/wc3-lan')).resolve()
dev = root / 'development'
data = root / 'data-current'
proof = dev / 'proof' / args.output
catalog_path = args.catalog or data / 'maps.index.txt'
catalog_text = catalog_path.read_text()
# The room catalog excludes rejected maps; the resource index retains all original inputs.
catalog = ([{'path': line.strip()} for line in catalog_text.splitlines() if line.strip()]
           if catalog_path.suffix == '.txt' else
           [json.loads(line) for line in catalog_text.splitlines() if line.strip()]
           if catalog_path.suffix == '.jsonl' else json.loads(catalog_text))
paths = [m['path'] for m in catalog]
for p in sorted((data / 'campaign/classic').rglob('*')):
    if p.suffix.lower() in ('.w3m', '.w3x'):
        with p.open('rb') as f: header = f.read(4096)
        if b'MPQ\x1a' in header: paths.append(p.relative_to(data).as_posix())
paths = list(dict.fromkeys(paths))
if args.retry_crashes:
    paths = [x['map'] for x in json.loads(args.retry_crashes.read_text()) if x['exitCode'] != 0]
elif args.retry_failures:
    paths = [x['map'] for x in json.loads(args.retry_failures.read_text())
             if not x['passed'] and (not error_match or any(error_match.search(e) for e in x['errors']))]
paths = list(dict.fromkeys(paths))
if args.list_only:
    print(json.dumps(dict(total=len(paths), maps=paths), ensure_ascii=False))
    raise SystemExit(0)
proof.mkdir(parents=True, exist_ok=True)

def check(pair):
    i, path = pair
    port = 28100 + i
    log = proof / f'{i:04d}.log'
    env = dict(os.environ, XDG_DATA_HOME=str(proof / f'user-{i}'))
    command = [str(args.engine or dev / 'src/build/classic/bin/openwarcraft3'), '-data', str(data),
               '+dedicated', '1', '+game_port', str(port), '+fs_expansion', '1',
               '+com_frame_limit', '100', '+com_fast_forward', '1', '+map', path]
    start, peak, limit = time.monotonic(), 0, None
    with log.open('w') as out:
        child = subprocess.Popen(command, cwd=dev / 'src', stdin=subprocess.DEVNULL,
                                 stdout=out, stderr=subprocess.STDOUT, env=env)
        while child.poll() is None:
            result = subprocess.run(['ps', '-o', 'rss=', '-p', str(child.pid)], capture_output=True, text=True)
            if result.stdout.strip(): peak = max(peak, int(result.stdout.strip()))
            if time.monotonic()-start > 45 or peak > 2*1024**2:
                limit = 'time-45s' if time.monotonic()-start > 45 else 'resident-2GiB'
                child.terminate()
                try: child.wait(timeout=3)
                except subprocess.TimeoutExpired: child.kill()
                break
            time.sleep(.25)
        child.wait()
    text = log.read_text(errors='replace')
    errors = [line for line in text.splitlines() if re.search(
        r'JASS runtime error|unsupported (?:placement|terrain|map|object)|Invalid map object|map load failed|invalid player count|invalid team count', line)]
    return dict(map=path, exitCode=child.returncode, seconds=round(time.monotonic()-start,2),
                residentKiB=peak, limit=limit, errors=errors, initialized='Server initialized.' in text,
                passed=child.returncode==0 and 'Server initialized.' in text and not errors)

results=[]
with ThreadPoolExecutor(max_workers=2) as pool:
    for future in as_completed([pool.submit(check, pair) for pair in enumerate(paths)]):
        result=future.result(); results.append(result)
        print(json.dumps({'done':len(results),'total':len(paths),**result}),flush=True)
        (proof/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'total':len(results),'passed':sum(r['passed'] for r in results),'failed':sum(not r['passed'] for r in results)}),flush=True)
