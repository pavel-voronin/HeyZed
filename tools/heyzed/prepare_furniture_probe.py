"""Validate nonstructural furniture bindings without reading map placement data."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent

DEFAULT_PACK = ROOT/'.local/furniture-source/assets.json'


def structural(tile, props):
    flags = ('solidfloor', 'wall', 'WallN', 'WallW', 'WallNW', 'DoorWallN', 'DoorWallW',
             'doorN', 'doorW', 'windowN', 'windowW', 'WindowN', 'WindowW', 'WindowNTrans',
             'WindowWTrans', 'WallSE', 'WallNTrans', 'WallWTrans', 'HoppableN', 'HoppableW')
    return (any(k in props for k in flags)
            or any(k.lower().startswith(('stairs', 'roof', 'westroof', 'northroof')) for k in props)
            or tile.startswith(('walls_', 'fencing_', 'fences_', 'fixtures_doors_',
                                'fixtures_windows_', 'fixtures_railings_', 'fixtures_escalators_', 'roofs_', 'rooftop_', 'ceilings_', 'carpentry_')))


def within(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError('Missing or escaping pack dependency: ' + str(path))
    return path


def bindings(pack, props):
    data = json.loads(pack.read_text(encoding='utf-8-sig'))
    if data['schema'].partition(':')[2] != 'assets/1':
        raise ValueError('Unsupported pack schema')
    result = defaultdict(list)
    rejected = Counter()
    for card in data['assets']:
        for variant, spec in card['variants'].items():
            for binding in spec.get('bindings', []):
                parts = binding.get('parts', [dict(tile=binding.get('tile'), x=0, y=0, z=0)])
                if any(p['tile'] not in props or structural(p['tile'], props[p['tile']]) for p in parts):
                    rejected['structural_or_unknown'] += 1
                    continue
                if len(parts) != 1:
                    rejected['assembly_requires_explicit_runtime_support'] += 1
                    continue
                if binding.get('layer', 'base') != 'base':
                    rejected['layer_selector'] += 1
                    continue
                if set(binding) - {'tile', 'parts', 'placement', 'state', 'layer', 'emitter'}:
                    rejected['conditional_selector'] += 1
                    continue
                root = (pack.parent / card['directory']).resolve()
                if not root.is_relative_to(pack.parent.resolve()):
                    raise ValueError('Escaping asset directory')
                anchor = next(p for p in parts if all(p.get(k, 0) == 0 for k in ('x', 'y', 'z')))
                result[anchor['tile']].append(dict(card=card['id'], variant=variant,
                    file=str(root / spec['file']), root=str(root), parts=parts,
                    state=binding.get('state', '*'), placement=binding.get('placement', {}),
                    emitter=binding.get('emitter', spec.get('emitter')), allowRepack=card['allowRepack'],
                    author=card['author'], url=card.get('url', '')))
    return result, rejected
