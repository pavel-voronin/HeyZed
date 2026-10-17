"""Offline floor-fact pack authoring; input is JSON snapshots or one snapshot per JSONL line."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import tempfile
import zlib

ROOT = Path(__file__).resolve().parent
PAGE = 64
MAGIC = b'HEYZEDWLD1'


def recipe():
    digest = hashlib.sha256(b'heyzed-ground-v1')
    for name in ('ground-shapes.json', 'ground-profiles.json', 'ground_geometry.py', 'materials/sources.json', 'prepare_assets.py', 'surface-material.ush'):
        digest.update((ROOT/name).read_text(encoding='utf-8').encode())
    return digest.hexdigest()


def snapshots(paths):
    for path in paths:
        with path.open(encoding='utf-8-sig') as stream:
            if path.suffix.lower() in ('.jsonl', '.ndjson'):
                for line in stream:
                    if line.strip(): yield json.loads(line)
            else:
                value = json.load(stream)
                yield from value if isinstance(value, list) else [value]


def build(inputs, output, map_id):
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    stamps = [(path.stat().st_size, path.stat().st_mtime_ns) for path in inputs]
    digest = hashlib.sha256()
    for path in inputs:
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024*1024), b''): digest.update(block)
    metadata = json.dumps(dict(map_id=map_id, recipe=recipe(), page_size=PAGE,
                               source_sha256=digest.hexdigest(), contents='floor_facts'),
                          separators=(',', ':')).encode()
    with tempfile.TemporaryDirectory(prefix='heyzed-world-', dir=output.parent) as temporary:
        folder = Path(temporary)
        database = sqlite3.connect(folder/'cells.sqlite')
        database.execute('CREATE TABLE cells (x INTEGER, y INTEGER, px INTEGER, py INTEGER, layers TEXT, PRIMARY KEY(x,y))')
        try:
            for snapshot in snapshots(inputs):
                if snapshot.get('schema') not in (1, 2): raise ValueError('Unsupported source snapshot')
                dictionary = snapshot['dictionary']
                for cell in snapshot['cells']:
                    x, y = cell[:2]
                    if type(x) is not int or type(y) is not int or not (-2**30 < x < 2**30 and -2**30 < y < 2**30):
                        raise ValueError('Invalid source cell address')
                    layers = []
                    for index in cell[2]:
                        if type(index) is not int or not 0 <= index < len(dictionary): raise ValueError('Invalid tile index')
                        tile = dictionary[index]
                        if not all(isinstance(tile.get(field, ''), str) for field in ('name', 'material', 'edges')) or type(tile.get('overlay')) is not bool:
                            raise ValueError('Invalid tile definition')
                        layers.append(dict(name=tile['name'], material=tile.get('material', ''), overlay=tile['overlay'], edges=tile.get('edges', '')))
                    database.execute('INSERT OR REPLACE INTO cells VALUES (?,?,?,?,?)',
                                     (x, y, x//PAGE, y//PAGE, json.dumps(layers, separators=(',', ':'))))
            database.commit()
            database.execute('CREATE INDEX pages ON cells(px,py)')
            entries = []
            with (folder/'payload').open('wb') as payload:
                for px, py in database.execute('SELECT DISTINCT px,py FROM cells ORDER BY py,px'):
                    dictionary, indices, cells = [], {}, []
                    for x, y, layers in database.execute('SELECT x,y,layers FROM cells WHERE px=? AND py=? ORDER BY y,x', (px, py)):
                        ids = []
                        for layer in json.loads(layers):
                            key = json.dumps(layer, sort_keys=True, separators=(',', ':'))
                            if key not in indices:
                                indices[key] = len(dictionary); dictionary.append(layer)
                            ids.append(indices[key])
                        cells.append([x, y, ids, 0, 1])
                    raw = json.dumps(dict(schema=2, epoch=0, revision=0, dictionary=dictionary, cells=cells), separators=(',', ':')).encode()
                    encoded = zlib.compress(raw, 6)
                    if len(raw) > 32*1024*1024 or len(encoded) > 32*1024*1024: raise ValueError('World page exceeds native limit')
                    entries.append((px, py, payload.tell(), len(encoded), len(raw), zlib.crc32(encoded)))
                    payload.write(encoded)
            candidate = folder/'world.pack'
            with candidate.open('wb') as archive, (folder/'payload').open('rb') as payload:
                archive.write(MAGIC+struct.pack('<III', 1, len(metadata), len(entries)))
                archive.write(metadata)
                for entry in entries: archive.write(struct.pack('<iiQIII', *entry))
                shutil.copyfileobj(payload, archive)
                archive.flush(); os.fsync(archive.fileno())
            if stamps != [(path.stat().st_size, path.stat().st_mtime_ns) for path in inputs]:
                raise RuntimeError('Source changed during preparation')
            os.replace(candidate, output)
        finally:
            database.close()
    print(f'{output}: {len(entries)} prepared floor pages')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--map-id', required=True)
    args = parser.parse_args()
    if not args.map_id.strip(): parser.error('map-id must identify the ordered source map set')
    build(args.source, args.output, args.map_id)


if __name__ == '__main__':
    main()
