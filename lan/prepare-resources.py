#!/usr/bin/env python3
"""Extract a supplied WC3 CASC install and build a verified zhCN SD mirror.

Run on the game workstation, outside the served web root. Each phase writes
its own evidence; no phase treats an existing partial directory as complete.
"""
import argparse
import base64
import csv
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import zipfile


def dump(path, obj):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def unpack(args):
    archive, target = Path(args.archive), Path(args.install)
    index = json.loads(Path(args.index).read_text())
    if archive.stat().st_size != index['bytes']:
        raise ValueError('Archive size differs from the inspected source')
    expected_hash = Path(args.sha256).read_text().split()[0]
    actual_hash = sha(archive)
    if actual_hash != expected_hash:
        raise ValueError('Archive SHA-256 differs after transfer')
    print('Archive transfer SHA-256 verified', actual_hash, flush=True)
    if target.exists():
        raise FileExistsError('Use a fresh installation output directory')
    partial = target.with_name(target.name + '.partial')
    partial.mkdir()
    expected = {item['name']: item for item in index['entries']}
    roots = {PurePosixPath(name).parts[0] for name in expected}
    if len(roots) != 1:
        raise ValueError('Expected exactly one game root')
    root = roots.pop()
    manifest = []
    try:
        with zipfile.ZipFile(archive) as z:
            selected = [i for i in z.infolist() if i.filename in expected]
            if len(selected) != len(expected):
                raise ValueError('Missing or duplicated ZIP entries')
            for i in selected:
                p = PurePosixPath(i.filename)
                mode = i.external_attr >> 16
                if p.is_absolute() or any(v in ('.', '..') for v in p.parts) or stat.S_ISLNK(mode):
                    raise ValueError('Unsafe ZIP entry')
                rel = p.relative_to(root)
                if not (str(rel) == '.build.info' or rel.parts[0] == 'Data'):
                    raise ValueError('Unexpected non-data file')
                e = expected[i.filename]
                if i.file_size != e['bytes'] or i.CRC != e['crc32']:
                    raise ValueError('ZIP entry metadata mismatch')
                dest = partial / str(rel)
                dest.parent.mkdir(parents=True, exist_ok=True)
                h = hashlib.sha256()
                size = 0
                with z.open(i) as src, dest.open('xb') as out:
                    for block in iter(lambda: src.read(4 * 1024 * 1024), b''):
                        out.write(block)
                        h.update(block)
                        size += len(block)
                # zipfile checks CRC at EOF; enforce decoded length as well.
                if size != i.file_size:
                    raise ValueError('Short ZIP extraction')
                manifest.append(dict(path=str(rel), bytes=size, sha256=h.hexdigest(), crc32=i.CRC))
                if len(manifest) % 25 == 0 or size > 500 * 1024 * 1024:
                    print('Unpacked', len(manifest), str(rel), size, flush=True)
        partial.rename(target)
        dump(Path(args.report), dict(archiveSha256=actual_hash, files=manifest,
                                    bytes=sum(i['bytes'] for i in manifest), crcVerified=True))
    except Exception:
        # Keep a clearly marked partial tree for diagnosis, never use it as an install.
        raise


def normalized(name):
    return name.replace(':', '/').replace('\\', '/').lower()


def select(args):
    available, unavailable, excluded = {}, [], {}
    with Path(args.catalog).open() as f:
        for size, present, name in csv.reader(f, delimiter='\t'):
            path = normalized(name)
            if not path.startswith('war3.w3mod/') or path.endswith('/'):
                continue
            rel = path[len('war3.w3mod/'):]
            reason = None
            if rel.startswith('_locales/'):
                if not rel.startswith('_locales/zhcn.w3mod/'):
                    reason = 'other-locale'
                else:
                    rel = rel[len('_locales/zhcn.w3mod/'):]
            # These are separate layered products, not classic SD assets.
            if any(part.startswith('_') and part.endswith('.w3mod') for part in rel.split('/')):
                reason = 'optional-product-layer'
            if rel.startswith(('webui/', 'movies/')):
                reason = 'reforged-web-ui-or-movie'
            if rel.startswith('campaign/reforged/'):
                reason = 'reforged-campaign'
            if not rel:
                reason = 'directory'
            if reason:
                excluded[reason] = excluded.get(reason, 0) + 1
                continue
            record = dict(name=name, bytes=int(size), relativePath=rel,
                          locale='zhCN' if '/_locales/zhcn.w3mod/' in path else 'base')
            if present != '1':
                unavailable.append(record)
                continue
            previous = available.get(path)
            if previous and previous != record:
                raise ValueError('Conflicting case-insensitive CASC names: ' + path)
            available[path] = record
    if not available:
        raise ValueError('No classic game assets were found')
    records = [available[k] for k in sorted(available)]
    Path(args.names).write_text(''.join(r['name'] + '\n' for r in records))
    summary = dict(selectedFiles=len(records), selectedBytes=sum(r['bytes'] for r in records),
                   excluded=excluded, unavailableFiles=len(unavailable), unavailable=unavailable, files=records)
    dump(Path(args.report), summary)
    print(json.dumps({k: v for k, v in summary.items() if k not in ('files', 'unavailable')}, indent=2), flush=True)


def needs_b64(data):
    if b'\0' in data or data.isascii():
        return False
    try:
        data.decode('utf-8')
    except UnicodeDecodeError:
        return False
    return True


def mirror(args):
    source, target = Path(args.extracted), Path(args.mirror)
    selection = json.loads(Path(args.selection).read_text())
    report = list(csv.DictReader(Path(args.extract_report).open(), delimiter='\t'))
    if len(report) != selection['selectedFiles'] or any(i['status'] != 'ok' for i in report):
        raise ValueError('CASC extraction did not complete successfully')
    by_name = {r['name']: int(r['bytes']) for r in report}
    merged = {}
    # Choose the final locale layer before creating sidecars and hashes.
    for record in sorted(selection['files'], key=lambda r: r['locale'] != 'base'):
        p = source / record['name'].replace(':', '/').replace('\\', '/')
        if not p.is_file() or p.stat().st_size != by_name[record['name']]:
            raise ValueError('Missing or partial extracted file: ' + str(p))
        rel = PurePosixPath(record['relativePath'])
        if rel.is_absolute() or '..' in rel.parts:
            raise ValueError('Unsafe mirror path')
        merged[str(rel)] = (p, record)
    if target.exists():
        raise FileExistsError('Use a fresh mirror directory')
    target.mkdir()
    files, b64, maps = [], [], []
    for rel in sorted(merged):
        p, record = merged[rel]
        dest = target / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dest)
        files.append(dict(path=rel, bytes=dest.stat().st_size, sha256=sha(dest),
                          source=record['name'], locale=record['locale']))
        if dest.stat().st_size <= 4 * 1024 * 1024:
            data = dest.read_bytes()
            if needs_b64(data):
                sidecar = target / (rel + '.b64')
                sidecar.write_bytes(base64.b64encode(data))
                b64.append(rel)
        if rel.startswith('maps/') and dest.suffix in ('.w3m', '.w3x'):
            with dest.open('rb') as f:
                header = f.read(8192)
            if b'MPQ\x1a' not in header:
                raise ValueError('Map is not an MPQ archive: ' + rel)
            maps.append(rel)
    required = ['ui/framedef/globalstrings.fdf', 'ui/campaigninfoclassic.txt',
                'units/unitdata.slk', 'scripts/common.j', 'fonts/frizqt__.ttf',
                'maps/frozenthrone/(2)banditridge.w3x']
    for rel in required:
        if not (target / rel).is_file() or not (target / rel).stat().st_size:
            raise ValueError('Required runtime asset absent: ' + rel)
    (target / 'b64.index.txt').write_text('\n'.join(b64) + '\n')
    (target / 'maps.index.txt').write_text('\n'.join(maps) + '\n')
    summary = dict(version='3.0.1.24371', locale='zhCN', graphics='classic-SD',
                   assetFiles=len(files), assetBytes=sum(f['bytes'] for f in files),
                   maps=len(maps), base64Sidecars=len(b64), strictCascReads=True,
                   runtimeGameplayVerified=False, files=files)
    dump(Path(args.report), summary)
    print(json.dumps({k: v for k, v in summary.items() if k != 'files'}, indent=2), flush=True)


def pack(args):
    mirror_dir, output = Path(args.mirror), Path(args.output)
    if output.exists():
        raise FileExistsError('Use a fresh resource package path')
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_suffix(output.suffix + '.partial')
    with zipfile.ZipFile(partial, 'w', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as z:
        for p in sorted(mirror_dir.rglob('*')):
            if p.is_file():
                z.write(p, 'gamedata/' + p.relative_to(mirror_dir).as_posix())
        z.write(args.manifest, 'resources-manifest.json')
        z.writestr('使用说明.txt',
                   'Winlife 魔兽争霸 III 网页资源包\n版本：3.0.1.24371；简体中文；经典画质 SD。\n'
                   '将 gamedata 目录配置为 wc3-wasm 网页客户端的 WC3_DATA_BASE。\n'
                   '该目录同时可供 OpenRealm 服务端的 -data 参数读取。\n'
                   '不包含 Windows 启动程序、重制版高清资源或过场视频。\n'
                   '文件来源及 SHA-256 见 resources-manifest.json；地图兼容性仍需按图验收。\n'
                   'Warcraft III 及游戏素材权利归 Blizzard Entertainment 所有。\n')
    partial.rename(output)
    with zipfile.ZipFile(output) as z:
        bad = z.testzip()
        if bad:
            raise ValueError('Package CRC verification failed: ' + bad)
        count = len(z.infolist())
    summary = dict(file=output.name, bytes=output.stat().st_size, sha256=sha(output),
                   entries=count, allEntryCrcVerified=True)
    dump(Path(args.report), summary)
    print(json.dumps(summary, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='phase', required=True)
    p = subs.add_parser('unpack')
    for name in ('archive', 'install', 'index', 'sha256', 'report'):
        p.add_argument('--' + name, required=True)
    p.set_defaults(func=unpack)
    p = subs.add_parser('select')
    for name in ('catalog', 'names', 'report'):
        p.add_argument('--' + name, required=True)
    p.set_defaults(func=select)
    p = subs.add_parser('mirror')
    for name in ('extracted', 'mirror', 'selection', 'extract-report', 'report'):
        p.add_argument('--' + name, required=True)
    p.set_defaults(func=mirror)
    p = subs.add_parser('pack')
    for name in ('mirror', 'output', 'manifest', 'report'):
        p.add_argument('--' + name, required=True)
    p.set_defaults(func=pack)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
