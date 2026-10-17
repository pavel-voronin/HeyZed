import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
from functools import lru_cache
from PIL import Image
from visible_images import visible_images

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('resource_inventory', ROOT.parents[1] / 'build-tools/tile-inventory.py')
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)
SCHEMA = 3


def encoded(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=True, separators=(',', ':')).encode()


def texture_lookup_keys(name):
    normalized = name.replace('\\', '/')
    if not normalized.endswith('.txt') and '/mods/' not in normalized:
        if normalized.endswith('.png'):
            normalized = normalized[:-4]
        normalized = normalized.rsplit('/', 1)[-1]
        return list(dict.fromkeys((normalized, name)))
    return [name]


def pack_entries(path):
    def string(f):
        size = inventory.int32(f)
        if not 0 <= size <= 1048576:
            raise ValueError('Invalid pack string')
        return f.read(size).decode('utf-8')
    with path.open('rb') as f:
        magic = f.read(4)
        version = inventory.int32(f) if magic == b'PZPK' else 0
        if not version:
            f.seek(0)
        if version not in (0, 1):
            raise ValueError('Unsupported texture pack version')
        rows = []
        for page_index in range(inventory.int32(f)):
            page, count, mask = string(f), inventory.int32(f), inventory.int32(f)
            entries = []
            for index in range(count):
                name = string(f)
                rect = list(struct.unpack('<8i', f.read(32)))
                entries.append(dict(name=name, page=page, pageIndex=page_index, entry=index,
                                    rect=rect[:4], offset=rect[4:6], originalSize=rect[6:], alpha=bool(mask)))
            if version:
                length = inventory.int32(f)
                if length < 0:
                    raise ValueError('Invalid image size')
                offset = f.tell()
                f.seek(length, 1)
            else:
                offset = f.tell()
                tail = b''
                while True:
                    data = f.read(65536)
                    if not data:
                        raise EOFError('Missing legacy page terminator')
                    combined = tail + data
                    pos = combined.find(b'\xef\xbe\xad\xde')
                    if pos >= 0:
                        f.seek(-(len(combined) - pos - 4), 1)
                        length = f.tell() - offset - 4
                        break
                    tail = combined[-3:]
            if f.tell() > path.stat().st_size:
                raise EOFError('Truncated texture page')
            for entry in entries:
                entry.update(imageOffset=offset, imageLength=length)
            rows.extend(entries)
        if f.read():
            raise ValueError('Trailing pack bytes')
        return rows


def source_files(game):
    return sorted(p for p in (game / 'media').rglob('*') if p.is_file())


def signature(game, files):
    return hashlib.sha256(encoded([str(game.resolve()), [(p.relative_to(game).as_posix(), p.stat().st_size, p.stat().st_mtime_ns) for p in files]])).hexdigest()


def native_inventory(game, local):
    source = ROOT.parents[1] / 'build-tools/TileInventory.java'
    stamp = hashlib.sha256(encoded([inventory.sha(source), inventory.sha(game / 'projectzomboid.jar'),
        [(p.name, inventory.sha(p)) for p in sorted((game / 'media').glob('*.tiles'))],
        signature(game, source_files(game))])).hexdigest()
    folder = local / 'native'
    folder.mkdir(exist_ok=True)
    output = folder / 'native-tiles.jsonl'
    marker = folder / 'signature.txt'
    if not output.exists() or not marker.exists() or marker.read_text() != stamp:
        compiler = Path(tempfile.gettempdir()) / 'PZModBuild/ecj-3.44.0.jar'
        if not compiler.exists():
            raise FileNotFoundError('Native inventory compiler is missing: ' + str(compiler))
        java = game / 'jre64/bin/java.exe'
        classes = folder / 'classes'
        classes.mkdir(exist_ok=True)
        jar = game / 'projectzomboid.jar'
        with (folder / 'probe.log').open('wb') as log:
            subprocess.run([str(java), '-jar', str(compiler), '-25', '-encoding', 'UTF-8', '-proc:none', '-nowarn',
                            '-classpath', str(jar), '-d', str(classes), str(source)], stdout=log, stderr=log, check=True)
            subprocess.run([str(java), '-cp', str(classes) + os.pathsep + str(jar), 'TileInventory', str(game),
                            str(output.resolve()), str((folder / 'cache').resolve())], cwd=game, stdout=log, stderr=log, check=True)
        marker.write_text(stamp)
    return [json.loads(line) for line in output.read_text('utf-8').splitlines()], json.loads((folder / 'native-probe.json').read_text('utf-8'))


def build(game, files, native=None, probe=None, native_error=None):
    rows, definitions, sources, errors = [], {}, [], []
    paths = sorted((game / 'media').rglob('*.tiles'), key=lambda p: ('patch' in p.name, p.as_posix()))
    for path in paths:
        source = path.relative_to(game).as_posix()
        try:
            entries = inventory.definitions(path)
            sources.append(dict(path=source, kind='definitions', count=len(entries), sha256=inventory.sha(path)))
            for entry in entries:
                name = entry['name']
                if name not in definitions:
                    definitions[name] = dict(entry, properties=dict(entry['properties']), sources=[], layers=[])
                record = definitions[name]
                record['properties'].update(entry['properties'])
                record['sources'].append(source)
                record['layers'].append(dict(source=source, properties=entry['properties']))
        except Exception as error:
            errors.append(dict(source=source, error=str(error)))
    by_name = {}
    for path in files:
        source = path.relative_to(game).as_posix()
        extension = path.suffix.lower()
        rows.append(dict(id='file:' + source, name=path.name, kind='file', source=source,
                         extension=extension, bytes=path.stat().st_size, properties={},
                         preview=extension in ('.png', '.jpg', '.jpeg', '.gif', '.bmp', '.webp', '.dds')))
        if extension != '.pack':
            continue
        try:
            entries = pack_entries(path)
            sources.append(dict(path=source, kind='pack', count=len(entries), sha256=inventory.sha(path)))
            for entry in entries:
                record = dict(entry, id=f"sprite:{source}:{entry['pageIndex']}:{entry['entry']}",
                              kind='sprite', source=source, properties=definitions.get(entry['name'], {}).get('properties', {}), preview=True)
                rows.append(record)
                by_name.setdefault(entry['name'], []).append(record)
        except Exception as error:
            errors.append(dict(source=source, error=str(error)))
    missing = []
    for entry in native or []:
        name = entry['name']
        if name not in definitions:
            definitions[name] = dict(name=name, properties={}, sources=['native:IsoSpriteManager'], layers=[])
        definitions[name]['native'] = entry
        for variant in by_name.get(name, []):
            variant['native'] = entry
    if native_error:
        errors.append(dict(source='native:IsoSpriteManager', error=native_error))
    raster_files = {p.relative_to(game).as_posix() for p in files if p.suffix.lower() in ('.png', '.jpg', '.jpeg', '.gif', '.bmp', '.webp', '.dds')}
    for name, record in definitions.items():
        lookup_keys = texture_lookup_keys(name)
        resolved_key = next((key for key in lookup_keys if by_name.get(key)), None)
        variants = list(by_name.get(resolved_key, []))
        variants.sort(key=lambda r: ('2x' not in r['source'].lower(), '.floor.' in r['source'], r['id']))
        row = dict(record, id='tile:' + name, kind='tile', source=record['sources'][0], preview=bool(variants), variants=[v['id'] for v in variants],
                   rasterLookup=dict(keys=lookup_keys, resolved=resolved_key, status='packed' if variants else 'no-direct-raster'))
        if not variants and name in raster_files:
            row.update(preview=True, image='file:' + name)
            row['rasterLookup'].update(resolved=name, status='file')
        if variants:
            row['image'] = variants[0]['id']
        elif not row['preview']:
            missing.append(name)
        rows.append(row)
    rows.sort(key=lambda row: (row['name'].lower(), row['kind'], row['id']))
    manifest = dict(schema=SCHEMA, game=str(game.resolve()), fileSignature=signature(game, files), sources=sources, errors=errors,
                    nativeCount=len(native or []), nativeProbe=probe,
                    counts={kind: sum(r['kind'] == kind for r in rows) for kind in ('tile', 'sprite', 'file')},
                    files=len(files), missingTileImages=missing,
                    coverage='complete' if not errors else 'incomplete')
    manifest['id'] = hashlib.sha256(encoded(manifest)).hexdigest()
    return dict(manifest=manifest, rows=rows)


class Catalog:
    def __init__(self, game, local=None):
        self.game = Path(game)
        self.local = local or ROOT / '.local'
        self.local.mkdir(parents=True, exist_ok=True)
        files = source_files(self.game)
        stamp = hashlib.sha256(encoded([signature(self.game, files), inventory.sha(Path(__file__)),
                                        inventory.sha(ROOT.parents[1] / 'build-tools/TileInventory.java')])).hexdigest()
        cache = self.local / 'catalog.json'
        data = json.loads(cache.read_text('utf-8')) if cache.exists() else {}
        if data.get('signature') != stamp or data.get('manifest', {}).get('schema') != SCHEMA:
            try:
                native, probe = native_inventory(self.game, self.local)
                native_error = None
            except Exception as error:
                native, probe, native_error = [], None, str(error)
            data = build(self.game, files, native, probe, native_error)
            data['signature'] = stamp
            temporary = cache.with_name(cache.name + '.' + os.urandom(6).hex() + '.tmp')
            temporary.write_bytes(encoded(data))
            temporary.replace(cache)
        self.manifest, self.rows = data['manifest'], data['rows']
        self.by_id = {row['id']: row for row in self.rows}
        images = visible_images(self.game, self.rows, self.local, self.manifest['id'])
        self.visible_ids = frozenset(images['ids'])
        self.image_frames = images['frames']
        self.image_errors = images['errors']
        for identity, image in images['tileImages'].items():
            self.by_id[identity]['image'] = image
        self.keys = sorted({k for row in self.rows if row['id'] in self.visible_ids for k in row['properties']})
        self.image_lock = threading.Lock()

    def field(self, row, key):
        if key.startswith('property:'):
            return row['properties'].get(key[9:])
        if key.startswith('native:'):
            value = row.get('native', {}).get(key[7:])
            return json.dumps(value, sort_keys=True) if isinstance(value, (dict, list)) else value
        value = row.get(key)
        return json.dumps(value, sort_keys=True) if isinstance(value, (dict, list)) else value

    def matches(self, row, condition):
        value = self.field(row, condition.get('key', 'name'))
        op, wanted = condition.get('op', 'contains'), str(condition.get('value', ''))
        if op == 'exists':
            return value is not None
        if op == 'missing':
            return value is None
        if value is None:
            return False
        text = str(value)
        if op == 'eq':
            return text == wanted
        if op == 'neq':
            return text != wanted
        if op == 'contains':
            return wanted.casefold() in text.casefold()
        if op == 'notContains':
            return wanted.casefold() not in text.casefold()
        if op in ('gt', 'lt'):
            try:
                return float(text) > float(wanted) if op == 'gt' else float(text) < float(wanted)
            except ValueError:
                return False
        raise ValueError('Unknown filter operator: ' + op)

    def select(self, query):
        search = str(query.get('search', '')).casefold()
        rules = query.get('rules', [])
        includes = [r for r in rules if not r.get('exclude')]
        excludes = [r for r in rules if r.get('exclude')]
        result = []
        for row in self.rows:
            if row['id'] not in self.visible_ids:
                continue
            if query.get('kind') and row['kind'] != query['kind']:
                continue
            if search and search not in (row['name'] + ' ' + row['source'] + ' ' + json.dumps(row['properties'], ensure_ascii=False)).casefold():
                continue
            if includes and not (any if query.get('mode') == 'any' else all)(self.matches(row, r) for r in includes):
                continue
            if any(self.matches(row, r) for r in excludes):
                continue
            result.append(row)
        return result

    def save(self, query):
        data = dict(schema=SCHEMA, catalog=self.manifest['id'], query=query, ids=[r['id'] for r in self.select(query)])
        identity = hashlib.sha256(encoded(data)).hexdigest()
        folder = self.local / 'selections'
        folder.mkdir(exist_ok=True)
        temporary = folder / (identity + '.' + os.urandom(6).hex() + '.tmp')
        temporary.write_bytes(encoded(dict(data, id=identity)))
        temporary.replace(folder / (identity + '.json'))
        return dict(id=identity, count=len(data['ids']), catalog=data['catalog'])

    @lru_cache(maxsize=2)
    def page(self, source, offset, length):
        with (self.game / source).open('rb') as stream:
            stream.seek(offset)
            image = Image.open(io.BytesIO(stream.read(length)))
            return image.convert('RGBA')

    def thumbnail(self, identity):
        row = self.by_id[identity]
        if row.get('image'):
            row = self.by_id[row['image']]
        with self.image_lock:
            if row['kind'] == 'sprite':
                image = self.page(row['source'], row['imageOffset'], row['imageLength'])
                x, y, w, h = row['rect']
                image = image.crop((x, y, x + w, y + h))
            elif row['kind'] == 'file' and row['preview']:
                with Image.open(self.game / row['source']) as source:
                    source.seek(self.image_frames.get(row['id'], 0))
                    image = source.convert('RGBA')
            else:
                raise ValueError('No raster image')
            image.thumbnail((160, 160))
            out = io.BytesIO()
            image.save(out, format='PNG')
            return out.getvalue()
