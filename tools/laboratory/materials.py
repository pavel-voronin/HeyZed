"""Shared material definitions owned by the Materials laboratory."""
import json
import hashlib
import os
import uuid
import math
import xml.etree.ElementTree as ET
from contextlib import contextmanager
from functools import lru_cache
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / 'materials-lab'
REGISTRY = ROOT / 'materials.json'


@lru_cache(maxsize=2)
def _read(revision):
    table = json.loads(REGISTRY.read_text('utf-8'))
    if table['schema'] != 'heyzed:construction-materials/1':
        raise ValueError('Unsupported material dictionary')
    if not set(table['tiles'].values()) <= set(table['materials']):
        raise ValueError('Material binding references an undeclared definition')
    if not set(table.get('approved', [])) <= set(table['materials']):
        raise ValueError('Approved material is undeclared')
    return table


def registry():
    stamp = REGISTRY.stat()
    return _read((stamp.st_size, stamp.st_mtime_ns))


def book_snapshot(path=REGISTRY):
    data = path.read_bytes()
    return hashlib.sha256(data).hexdigest(), json.loads(data)


@contextmanager
def book_lock(path):
    folder = path.parent / '.local'
    folder.mkdir(exist_ok=True)
    with (folder / 'bindings.lock').open('a+b') as lock:
        lock.seek(0, 2)
        if lock.tell() == 0: lock.write(b'0'); lock.flush()
        lock.seek(0)
        if os.name == 'nt':
            import msvcrt
            msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX)
        try: yield
        finally:
            lock.seek(0)
            if os.name == 'nt': msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else: fcntl.flock(lock, fcntl.LOCK_UN)


def save_bindings(payload, path=REGISTRY):
    with book_lock(path):
        revision, table = book_snapshot(path)
        if payload.get('revision') != revision:
            raise ValueError('Material dictionary changed; reload before assigning')
        history = table.setdefault('bindingHistory', [])
        if payload.get('action') == 'undo':
            if not history: raise ValueError('No material assignments to undo')
            changes = history[-1]['tiles']
            if any(table['tiles'].get(name) != change['after'] for name, change in changes.items()):
                raise ValueError('Material assignments changed after this operation')
            for name, change in changes.items():
                if change['before'] is None: table['tiles'].pop(name, None)
                else: table['tiles'][name] = change['before']
            history.pop()
        elif payload.get('action') in ('assign', 'clear', 'create'):
            names = sorted(set(payload.get('tiles', [])))
            if not names: raise ValueError('No source tiles selected')
            identity = payload.get('material') if payload['action'] == 'assign' else None
            if payload['action'] == 'create':
                if payload.get('candidate') and any(material.get('sourceCandidate') == payload['candidate'] for material in table['materials'].values()):
                    raise ValueError('This candidate already has a material')
                label = str(payload.get('label', '')).strip()
                if not label or len(label) > 160: raise ValueError('Material name required (max 160 characters)')
                identity = 'material:' + uuid.uuid4().hex
                table['materials'][identity] = dict(label=label, color='#8b9290', texture=None, status='draft', sourceTiles=names, sourceCandidate=payload.get('candidate'))
            record = table['materials'].get(identity, {})
            if identity and not (record.get('texture') or record.get('status') == 'draft'):
                raise ValueError('An authored material texture is required')
            if payload['action'] == 'assign' and not identity: raise ValueError('Material required')
            changes = {name: dict(before=table['tiles'].get(name), after=identity) for name in names if table['tiles'].get(name) != identity}
            if changes:
                for name in changes:
                    if identity: table['tiles'][name] = identity
                    else: table['tiles'].pop(name, None)
                table['bindingHistory'] = [*history[-39:], dict(tiles=changes)]
        else: raise ValueError('Unknown material assignment action')
        temporary = path.with_suffix('.' + uuid.uuid4().hex + '.tmp')
        temporary.write_bytes((json.dumps(table, ensure_ascii=False, indent=2) + '\n').encode())
        temporary.replace(path)
        return dict(revision=book_snapshot(path)[0], changed=len(changes), undo=len(table['bindingHistory']), material=identity if payload.get('action') == 'create' else None)


def texture_path(path):
    file = (ROOT / path).resolve()
    if not file.is_relative_to(ROOT / 'public/materials') or not file.is_file():
        raise ValueError('Material texture must exist inside the shared material asset directory')
    return file


@lru_cache(maxsize=128)
def _texture_info(path, stamp):
    file = Path(path)
    if file.suffix.lower() == '.svg':
        node = ET.fromstring(file.read_bytes())
        width, height = (int(float(node.attrib[key].removesuffix('px'))) for key in ('width', 'height'))
        fmt, mode = 'SVG', 'vector'
    else:
        from PIL import Image
        with Image.open(file) as image:
            width, height = image.size
            fmt, mode = image.format, image.mode
    return dict(width=width, height=height, format=fmt, mode=mode, bytes=stamp[0],
                mipLevels=1+int(math.log2(max(width, height))))


def texture_info(path):
    file = texture_path(path)
    stamp = file.stat()
    return _texture_info(str(file), (stamp.st_size, stamp.st_mtime_ns))


def descriptor(identity, table=None):
    table = table or registry()
    material = table['materials'][identity]
    pbr = dict(table.get('pbrDefaults', {}), **material.get('pbr', {}))
    return dict(id=identity, label=material.get('label'), status='approved' if identity in table.get('approved', []) else 'draft' if material.get('status') == 'draft' else 'existing' if material.get('texture') else 'legacy',
                color=material['color'], reference=material.get('wallTextureSource') or material.get('initialReference'),
                texture=material.get('texture'), worldSize=material.get('textureWorldSize'), pbr=pbr,
                sourceCandidate=material.get('sourceCandidate'),
                textureInfo={channel: texture_info(path) for channel, path in dict(albedo=material.get('texture'), **material.get('maps', {})).items() if path},
                sourceMaps=material.get('sourceMaps', {}), normalFormat=material.get('normalFormat', 'opengl'),
                sampling=dict(wrap='repeat', mipmaps=material.get('mipmaps', True)),
                maps={channel: material.get('maps', {}).get(channel) for channel in ('normal', 'roughness', 'metallic', 'occlusion', 'height', 'emission')})
