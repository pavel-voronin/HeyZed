"""Prepare installed B42 map facts and merged surfaces for native Unreal authoring."""
from array import array
from concurrent.futures import ProcessPoolExecutor
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent/'roof-lab'))
from pzdata import header

VERSION = 1
PROPS = {}
SHAPES = {}


def initialize(catalog, shapes):
    global PROPS, SHAPES
    source = json.loads(Path(catalog).read_text(encoding='utf-8'))
    rows = source['rows']
    PROPS = {row['name']: row.get('properties', {}) for row in rows if row['kind'] == 'tile'}
    SHAPES = json.loads(Path(shapes).read_text(encoding='utf-8'))['overlays']


def tile(name, overlay):
    p = PROPS.get(name, {})
    return dict(name=name, material=p.get('FloorMaterial', p.get('FootstepMaterial', '')),
                overlay=overlay, edges=''.join(d for d in 'NESW' if 'FloorAttachment'+d in p))


def surface(material):
    return 'Grass' if material.startswith('Grass') else material or 'Unknown'


def atomic(path, data):
    pending = path.with_suffix(path.suffix+'.pending')
    pending.write_bytes(data)
    os.replace(pending, path)


def prepare(job):
    header_name, pack_name, folder, stamp = job
    output = Path(folder)/Path(header_name).stem
    record = output.with_suffix('.json')
    if record.exists():
        old = json.loads(record.read_text())
        if old.get('stamp') == stamp and output.with_suffix('.pzf').exists():
            return old['summary']
    h = header(Path(header_name))
    data = Path(pack_name).read_bytes()
    table = 12 if data[:4] == b'LOTP' else 4
    unpack = struct.Struct('<i').unpack_from
    palettes, palette, index = [()], {(): 0}, array('H', [0])*65536
    definitions = []
    for name in h['names']:
        p = PROPS.get(name, {})
        definitions.append((name, 'solidfloor' in p, 'FloorOverlay' in p or 'attachedFloor' in p))
    for cx in range(32):
        for cy in range(32):
            pos = unpack(data, table+(cx*32+cy)*8)[0]
            if pos < 0:
                continue
            if not table+8192 <= pos < len(data):
                raise ValueError('Invalid native map chunk offset: '+pack_name)
            cell, last = 0, (1-h['low'])*64
            while cell < last:
                count = unpack(data, pos)[0]; pos += 4
                if count == -1:
                    skip = unpack(data, pos)[0]; pos += 4
                    if skip <= 0: raise ValueError('Invalid native empty run')
                    cell += skip
                    continue
                if count > 1:
                    if count > 4096: raise ValueError('Invalid native stack')
                    pos += 4
                    ids = struct.unpack_from('<'+str(count-1)+'i', data, pos); pos += (count-1)*4
                    if cell >= -h['low']*64:
                        floor, overlays = None, []
                        for identity in ids:
                            name, solid, overlay = definitions[identity]
                            if solid and floor is None:
                                floor = name
                            elif overlay:
                                overlays.append(name)
                        names = ((floor, False),) if floor else ()
                        names += tuple((name, True) for name in overlays)
                        value = palette.get(names)
                        if value is None:
                            value = len(palettes); palette[names] = value; palettes.append(names)
                        xy = cell % 64
                        index[(cy*8+xy%8)*256+cx*8+xy//8] = value
                cell += 1
    layers = [[tile(name, overlay) for name, overlay in names] for names in palettes]
    base = [next((surface(t['material']) for t in ls if not t['overlay']), '') for ls in layers]
    style_indices, styles, ids, paint = array('H', [0])*65536, [None], {}, []
    for y in range(256):
        for x in range(256):
            address = y*256+x; selected = index[address]; ls = layers[selected]
            material = base[selected]
            if not material: continue
            blend, edges = material, ''
            curb = any(t['name'].startswith('street_curbs_') for t in ls)
            for t in ls:
                if t['overlay'] and t['material'] and t['edges'] and not t['name'].startswith('street_curbs_'):
                    if surface(t['material']) != material: blend, edges = surface(t['material']), t['edges']
            if curb and not edges:
                candidates = {}
                for dx,dy,d in [(0,-1,'N'),(1,0,'E'),(0,1,'S'),(-1,0,'W'),(1,-1,'NE'),(1,1,'SE'),(-1,1,'SW'),(-1,-1,'NW')]:
                    if not (0 <= x+dx < 256 and 0 <= y+dy < 256): continue
                    other = base[index[(y+dy)*256+x+dx]]
                    if other and other != material and (material.startswith('Road') or other.startswith('Road')):
                        if len(d) == 1 or not candidates: candidates[other] = candidates.get(other, '')+d
                if candidates: blend, edges = max(candidates.items(), key=lambda p: len(p[1]))
            nx, ny = int('W' in edges)-int('E' in edges), int('N' in edges)-int('S' in edges)
            length = (nx*nx+ny*ny)**.5
            nx, ny = (nx/length, ny/length) if length else (0,0)
            key = (material, blend, nx, ny, int(curb and blend != material and length > 0))
            if key not in ids: ids[key] = len(styles); styles.append(key)
            style_indices[address] = ids[key]
            for t in ls:
                if t['overlay'] and t['name'] in SHAPES and not t['name'].startswith(('blends_', 'street_curbs_')):
                    paint.append([x, y, t['name']])
    # Only uniform surfaces can span cells: a boundary's plane offset belongs to its original square.
    visited = bytearray(65536); rects = []
    for y in range(256):
        for x in range(256):
            at = y*256+x; style = style_indices[at]
            if not style or visited[at]: continue
            width, height = 1, 1
            if styles[style][0] == styles[style][1]:
                while x+width < 256 and not visited[at+width] and style_indices[at+width] == style: width += 1
                while y+height < 256:
                    start = (y+height)*256+x
                    if any(visited[start+i] or style_indices[start+i] != style for i in range(width)): break
                    height += 1
            for dy in range(height): visited[(y+dy)*256+x:(y+dy)*256+x+width] = b'\1'*width
            rects.append([x,y,width,height,style])
    facts = json.dumps(layers, separators=(',', ':')).encode()
    raw = struct.pack('>4siiiI', b'PZBF', VERSION, h['cx'], h['cy'], len(facts))+facts
    grid = array('H', index)
    if sys.byteorder == 'little': grid.byteswap()
    atomic(output.with_suffix('.pzf'), zlib.compress(raw+grid.tobytes(), 6))
    render_grid = array('H', style_indices)
    if sys.byteorder == 'big': render_grid.byteswap()
    atomic(output.with_suffix('.grid'), zlib.compress(render_grid.tobytes(), 6))
    summary = dict(cells=1, squares=sum(i != 0 for i in index), rectangles=len(rects), paint=len(paint), bytes=output.with_suffix('.pzf').stat().st_size)
    atomic(record, json.dumps(dict(version=VERSION, stamp=stamp, x=h['cx'], y=h['cy'], styles=styles, rects=rects, paint=paint, summary=summary), separators=(',', ':')).encode())
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', type=Path, default=Path('C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid'))
    parser.add_argument('--map', default='Muldraugh, KY')
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    catalog = ROOT.parent/'resources-lab/.local/catalog.json'
    maps = args.game/'media/maps'/args.map
    headers = sorted(maps.glob('*.lotheader'))
    if not headers: raise RuntimeError('No installed B42 map cells')
    stamps = []
    for path in headers:
        pack = maps/('world_'+path.stem+'.lotpack')
        stamps.append([str(path.resolve()),path.stat().st_size,path.stat().st_mtime_ns//1000000,str(pack.resolve()),pack.stat().st_size,pack.stat().st_mtime_ns//1000000])
    fingerprint = hashlib.sha256(json.dumps([VERSION,stamps,hashlib.sha256(catalog.read_bytes()).hexdigest(),hashlib.sha256(Path(__file__).read_bytes()).hexdigest()]).encode()).hexdigest()
    folder = ROOT/'.local/world'/fingerprint
    folder.mkdir(parents=True, exist_ok=True)
    jobs = [(stamp[0],stamp[3],str(folder),stamp) for stamp in stamps]
    totals = dict(cells=0,squares=0,rectangles=0,paint=0,bytes=0)
    with ProcessPoolExecutor(max_workers=args.workers, initializer=initialize, initargs=(str(catalog),str(ROOT/'ground-shapes.json'))) as pool:
        for n, result in enumerate(pool.map(prepare, jobs, chunksize=2), 1):
            for key, value in result.items(): totals[key] += value
            if n % 100 == 0: print(f'Prepared {n}/{len(jobs)} native cells', flush=True)
    manifest = dict(version=VERSION, identity=fingerprint, maps=[args.map], source_files=stamps, folder=str(folder), totals=totals, world='/Game/HeyZed/Baked/W'+fingerprint[:16]+'/World')
    atomic(folder/'manifest.json', json.dumps(manifest, separators=(',', ':')).encode())
    atomic(ROOT/'.local/world/current.json', json.dumps(manifest, separators=(',', ':')).encode())
    installed = Path.home()/'.heyzed/world'; installed.mkdir(parents=True, exist_ok=True)
    atomic(installed/'current.json', json.dumps(manifest, separators=(',', ':')).encode())
    print(json.dumps(totals), flush=True)


if __name__ == '__main__': main()
