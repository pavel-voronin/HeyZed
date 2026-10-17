"""Standalone ground review server. Never writes renderer resources."""
import argparse
import array
from functools import lru_cache
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
from pathlib import Path
import re
import struct
import sys
import threading
from urllib.parse import urlparse, unquote, parse_qs
import zlib

from PIL import Image
from material_preview import Materials

ROOT = Path(__file__).resolve().parent
BRIDGE = ROOT.parent
sys.path.insert(0, str(BRIDGE))
from ground_geometry import compile_catalog

COLORS = dict(Road_01='#646972', Road_02='#747881', Road_03='#525761', Road_04='#a19788', Road_05='#888681', Road_06='#64605b', Road_07='#77716b', Grass='#54804a', Concrete='#c3bca7', Curb='#eee2c4', Gravel='#928775', Dirt='#96714e', Clay='#b97f59', Dirt_Grass='#817348', Burnt='#3e3936', Sand='#dcc48c', Unknown='#c25cba', PaintWhite='#f9f6e7', PaintYellow='#efc647', PaintHidden='#5b626a')


class Sources:
    def __init__(self, game, storage):
        packs = [game / 'media/texturepacks' / name for name in ('Tiles1x.pack', 'Tiles1x.floor.pack', 'Tiles2x.pack', 'Tiles2x.floor.pack', 'B42ChunkCaching2x.pack', 'B42ChunkCaching2x.floor.pack', 'Overlays2x.pack', 'Overlays2x.floor.pack')]
        packs = [p for p in packs if p.exists()]
        fingerprint = [[str(p), p.stat().st_size, p.stat().st_mtime_ns] for p in packs]
        cache = storage / 'sprite-index.json'
        saved = json.loads(cache.read_text('utf-8')) if cache.exists() else {}
        if saved.get('fingerprint') == fingerprint:
            self.entries = saved['entries']
            return
        self.entries = {}
        for pack in packs:
            with pack.open('rb') as f:
                def number():
                    return struct.unpack('<i', f.read(4))[0]
                def string():
                    return f.read(number()).decode('utf-8')
                modern = f.read(4) == b'PZPK'
                if modern:
                    if number() != 1:
                        raise ValueError('Unsupported texture pack')
                else:
                    f.seek(0)
                for _ in range(number()):
                    string()
                    count = number()
                    number()
                    rows = [(string(), [number() for _ in range(8)]) for _ in range(count)]
                    if modern:
                        length = number()
                        offset = f.tell()
                        f.seek(length, 1)
                    else:
                        offset = f.tell()
                        if f.read(8) != b'\x89PNG\r\n\x1a\n':
                            raise ValueError('Invalid atlas PNG')
                        while True:
                            length, kind = struct.unpack('>I4s', f.read(8))
                            f.seek(length + 4, 1)
                            if kind == b'IEND':
                                break
                        length = f.tell() - offset
                        if f.read(4) != b'\xef\xbe\xad\xde':
                            raise ValueError('Invalid atlas terminator')
                    for name, rect in rows:
                        self.entries[name] = [str(pack), offset, length, rect]
        cache.write_text(json.dumps(dict(fingerprint=fingerprint, entries=self.entries)), 'utf-8')

    @lru_cache(maxsize=6)
    def page(self, path, offset, length):
        with open(path, 'rb') as f:
            f.seek(offset)
            return Image.open(io.BytesIO(f.read(length))).convert('RGBA')

    @lru_cache(maxsize=512)
    def sprite(self, name):
        path, offset, length, (x, y, w, h, ox, oy, ow, oh) = self.entries[name]
        result = Image.new('RGBA', (ow, oh))
        result.paste(self.page(path, offset, length).crop((x, y, x+w, y+h)), (ox, oy))
        if '1x' in Path(path).name:
            result = result.resize((ow*2, oh*2), Image.Resampling.NEAREST)
        output = io.BytesIO()
        result.save(output, format='PNG')
        return output.getvalue()


class Laboratory:
    def __init__(self, game, storage):
        self.storage = storage
        storage.mkdir(parents=True, exist_ok=True)
        self.sources = Sources(game, storage)
        props = {}
        self.definition_files = {}
        for file in sorted((game / 'media').glob('*.tiles.txt')):
            for match in re.finditer(r'//\s*(\S+)\s*\n\s*tile\s*\{([^}]+)\}', file.read_text('utf-8-sig')):
                values = {a.strip(): b.strip() for a,b in re.findall(r'^\s*(\w+)\s*=([^\n]*)', match[2], re.M)}
                if '.patch.' in file.name:
                    props.setdefault(match[1], {}).update(values)
                else:
                    props[match[1]] = values
                self.definition_files.setdefault(match[1], []).append(file.name)
        if not props:
            raise ValueError('Native tile definitions missing: ' + str(game / 'media'))
        self.props = props
        self.catalog = compile_catalog(props)
        self.tiles = {}
        for name in sorted(set(self.catalog['tiles']) | {n for n, p in props.items() if n.startswith(('blends_street_', 'blends_natural_', 'street_curbs_', 'street_trafficlines_')) and 'FloorMaterial' in p}):
            keys = self.catalog['tiles'].get(name)
            material = props.get(name, {}).get('FloorMaterial', 'Unknown')
            if material.startswith('Grass'):
                material = 'Grass'
            overlay = 'FloorOverlay' in props.get(name, {}) or 'attachedFloor' in props.get(name, {})
            parts = [self.catalog['meshes'][k] for k in keys] if keys is not None else ([] if overlay else [dict(material=material, vertices=[[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]], outline=[[0,0],[1,0],[1,1],[0,1]])])
            self.tiles[name] = dict(name=name, family=name.rsplit('_', 1)[0], parts=parts, material=material, overlay=overlay, sprite=name in self.sources.entries, revision=hashlib.sha256(json.dumps(parts, sort_keys=True).encode()).hexdigest()[:16])
        self.bases = {}
        for name, p in sorted(props.items()):
            material = p.get('FloorMaterial')
            if name.startswith(('blends_street_01_', 'blends_natural_01_')) and name.rsplit('_', 1)[1].isdigit() and int(name.rsplit('_', 1)[1]) % 16 == 0 and material:
                self.bases.setdefault('Grass' if material.startswith('Grass') else material, name)
        self.review_file = storage / 'reviews.json'
        self.reviews = json.loads(self.review_file.read_text('utf-8')) if self.review_file.exists() else {}
        self.lock = threading.Lock()
        self.location_lock = threading.Lock()
        self.materials = Materials(BRIDGE)
        self.index_thread = None
        self.locations = None
        self.index_error = None

    @lru_cache(maxsize=2048)
    def describe(self, name):
        properties = self.props.get(name, {})
        tile = self.tiles.get(name)
        native_material = properties.get('FloorMaterial', properties.get('FootstepMaterial', ''))
        normalized = 'Grass' if native_material.startswith('Grass') else native_material or 'Unknown'
        geometry = tile['parts'] if tile else ([] if 'FloorOverlay' in properties or 'attachedFloor' in properties and 'solidfloor' not in properties else [dict(material=normalized, vertices=[[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]])])
        texture = self.sources.entries.get(name)
        return dict(properties=properties, definitions=self.definition_files.get(name, []), nativeMaterial=native_material,
                    geometry=[dict(material=p['material'], triangles=len(p['vertices'])//3,
                                   coverage=sum(abs((p['vertices'][i+1][0]-p['vertices'][i][0])*(p['vertices'][i+2][1]-p['vertices'][i][1])-(p['vertices'][i+1][1]-p['vertices'][i][1])*(p['vertices'][i+2][0]-p['vertices'][i][0]))/2 for i in range(0,len(p['vertices']),3))) for p in geometry],
                    sprite=dict(pack=Path(texture[0]).name, rectangle=texture[3]) if texture else None)

    @lru_cache(maxsize=12)
    def region(self, folder, rx, ry):
        raw = zlib.decompress((Path(folder) / f'{rx}_{ry}.pzf').read_bytes())
        magic, version, ax, ay, length = struct.unpack_from('>4siiiI', raw)
        if (magic, version, ax, ay) != (b'PZBF', 1, rx, ry):
            raise ValueError('Unsupported native floor cache')
        palette = json.loads(raw[20:20+length])
        indices = array.array('H', raw[20+length:])
        if sys.byteorder == 'little':
            indices.byteswap()
        return palette, indices

    def scene(self, x, y):
        current = json.loads((BRIDGE / '.local/world/current.json').read_text('utf-8'))
        cells = []
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                ax, ay = x+dx, y+dy
                try:
                    palette, indices = self.region(current['folder'], ax//256, ay//256)
                    layers = palette[indices[(ay%256)*256+ax%256]]
                except FileNotFoundError:
                    layers = []
                cells.append(dict(x=dx+1, y=dy+1, worldX=ax, worldY=ay, layers=[dict(layer, metadata=self.describe(layer['name'])) for layer in layers]))
        return dict(cells=cells, identity=current['identity'], centerX=x, centerY=y)

    def index_locations(self, current, locations):
        cache = self.storage / 'tile-locations.json'
        try:
            sources = sorted(Path(current['folder']).glob('*.pzf'), key=lambda p: (abs(int(p.stem.split('_')[0])-31)+abs(int(p.stem.split('_')[1])-45), p.name))
            found = locations['tiles']
            for source in sources[locations.get('scanned', 0):]:
                raw = zlib.decompress(source.read_bytes())
                magic, version, rx, ry, length = struct.unpack_from('>4siiiI', raw)
                if magic != b'PZBF' or version != 1:
                    raise ValueError('Unsupported native floor cache')
                palette = json.loads(raw[20:20+length])
                grid = raw[20+length:]
                for index, stack in enumerate(palette):
                    names = [t['name'] for t in stack if t['name'] in self.tiles and t['name'] not in found]
                    if not names:
                        continue
                    code = struct.pack('>H', index)
                    offset = grid.find(code)
                    while offset >= 0 and offset % 2:
                        offset = grid.find(code, offset+1)
                    if offset < 0:
                        continue
                    local_y, local_x = divmod(offset//2, 256)
                    for tile in names:
                        found[tile] = [rx*256+local_x, ry*256+local_y]
                locations['scanned'] += 1
                if locations['scanned'] % 32 == 0:
                    self.persist_locations(cache, locations)
            locations['complete'] = True
            self.persist_locations(cache, locations)
        except Exception as e:
            self.index_error = str(e)
            print('Native map index: '+str(e), file=sys.stderr, flush=True)

    def persist_locations(self, cache, locations):
        temporary = cache.with_suffix('.tmp')
        temporary.write_text(json.dumps(locations), 'utf-8')
        os.replace(temporary, cache)

    def neighborhood(self, name):
        if name not in self.tiles:
            raise ValueError('Unknown tile')
        current = json.loads((BRIDGE / '.local/world/current.json').read_text('utf-8'))
        with self.location_lock:
            if self.locations is None or self.locations.get('identity') != current['identity']:
                cache = self.storage / 'tile-locations.json'
                self.locations = json.loads(cache.read_text('utf-8')) if cache.exists() else {}
                if self.locations.get('identity') != current['identity']:
                    self.locations = dict(identity=current['identity'], tiles={}, scanned=0, complete=False)
                self.locations.setdefault('complete', 'scanned' not in self.locations)
            locations = self.locations
            if not locations['complete'] and (self.index_thread is None or not self.index_thread.is_alive()) and not self.index_error:
                self.index_thread = threading.Thread(target=self.index_locations, args=(current, locations), daemon=True)
                self.index_thread.start()
        point = locations['tiles'].get(name)
        if point:
            result = self.scene(*point)
            result['status'] = f'Native map | {point[0]}, {point[1]}'
            return result
        tile = self.tiles[name]
        material = 'Road_01'
        base = dict(name=self.bases[material], material=material, overlay=False)
        cells = [dict(x=x, y=y, layers=[base] + ([dict(name=name, material=tile['material'], overlay=tile['overlay'])] if (x,y)==(1,1) else [])) for y in range(3) for x in range(3)]
        for cell in cells:
            cell['layers'] = [dict(layer, metadata=self.describe(layer['name'])) for layer in cell['layers']]
        pending = not locations['complete'] and not self.index_error
        return dict(cells=cells, identity='base-context', centerX=None, centerY=None, pending=pending, status=('Searching native map | Base context' if pending else ('Map index error: '+self.index_error if self.index_error else 'No map occurrence | Base context')))

    def save(self, value):
        name = value.get('name')
        if name not in self.tiles or value.get('revision') != self.tiles[name]['revision']:
            raise ValueError('Unknown or changed tile')
        if value.get('status') not in ('Unreviewed', 'Approved', 'Rework'):
            raise ValueError('Invalid review status')
        if len(value.get('note', '')) > 4000 or len(value.get('markers', [])) > 200:
            raise ValueError('Review too large')
        for marker in value.get('markers', []):
            if marker.get('issue') not in ('Shape', 'Material', 'Side', 'Join') or not all(isinstance(marker.get(k), (int, float)) and 0 <= marker[k] <= 1 for k in ('x', 'y')):
                raise ValueError('Invalid marker')
        with self.lock:
            self.reviews[name] = value
            temporary = self.review_file.with_suffix('.tmp')
            temporary.write_text(json.dumps(self.reviews, ensure_ascii=False, indent=2), 'utf-8')
            os.replace(temporary, self.review_file)


class Handler(BaseHTTPRequestHandler):
    def send(self, body, kind='application/json', status=200):
        if kind == 'application/json':
            body = json.dumps(body, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_GET(self):
        try:
            url = urlparse(self.path)
            if url.path == '/api/catalog':
                return self.send(dict(tiles=list(self.server.lab.tiles.values()), colors=COLORS, materials=list(self.server.lab.materials.recipes), bases=self.server.lab.bases, reviews=self.server.lab.reviews))
            if url.path == '/api/scene':
                q = parse_qs(url.query)
                return self.send(self.server.lab.scene(int(q['x'][0]), int(q['y'][0])))
            if url.path == '/api/neighborhood':
                q = parse_qs(url.query)
                return self.send(self.server.lab.neighborhood(q['name'][0]))
            if url.path.startswith('/api/material/'):
                q = parse_qs(url.query)
                return self.send(self.server.lab.materials.image(unquote(url.path[14:]), int(q.get('x', ['0'])[0]), int(q.get('y', ['0'])[0])), 'image/png')
            if url.path.startswith('/api/sprite/'):
                return self.send(self.server.lab.sources.sprite(unquote(url.path[12:])), 'image/png')
            path = (ROOT / 'dist' / unquote(url.path).lstrip('/')).resolve()
            if not path.is_relative_to((ROOT / 'dist').resolve()):
                raise ValueError('Invalid path')
            if url.path == '/':
                path = ROOT / 'dist/index.html'
            kinds = {'.html':'text/html; charset=utf-8', '.js':'text/javascript', '.css':'text/css'}
            return self.send(path.read_bytes(), kinds.get(path.suffix, 'application/octet-stream'))
        except (KeyError, FileNotFoundError) as e:
            self.send(dict(error=str(e)), status=404)
        except Exception as e:
            self.send(dict(error=str(e)), status=400)

    def do_POST(self):
        try:
            if self.path != '/api/review' or self.headers.get('Origin') != f'http://{self.headers.get("Host")}':
                return self.send(dict(error='Invalid origin or endpoint'), status=403)
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 100000:
                raise ValueError('Invalid request length')
            self.server.lab.save(json.loads(self.rfile.read(length)))
            self.send(dict(saved=True))
        except Exception as e:
            self.send(dict(error=str(e)), status=400)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8893)
    parser.add_argument('--game', type=Path, default=Path(os.environ.get('PZ_GAME_DIR', r'C:\Program Files (x86)\Steam\steamapps\common\ProjectZomboid')))
    parser.add_argument('--storage', type=Path, default=BRIDGE / '.local/ground-lab')
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.lab = Laboratory(args.game, args.storage)
    print(f'Unreal Ground Lab ready: http://127.0.0.1:{args.port} | {len(server.lab.tiles)} tiles', flush=True)
    server.serve_forever()
