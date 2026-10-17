"""Shared, reproducible approval for the window fracture generator."""
import json
import math
from pathlib import Path

TARGET = Path(__file__).with_name('window-fracture-preset.json')
RANGES = {'size': (.2, 1.2), 'strength': (.25, 8), 'grip': (.1, 3), 'woodDamage': (0, 2)}


def validate(data):
    if not isinstance(data, dict) or data.get('schema') != 'heyzed:window-fracture/1' or data.get('approved') is not True:
        raise ValueError('Invalid fracture preset')
    parameters = data.get('parameters', {})
    for key, (low, high) in RANGES.items():
        value = parameters.get(key)
        if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError('Invalid ' + key)
    reference = data.get('reference', {})
    from geometry.profile import descriptor
    profiles = {item['id'] for item in descriptor()['openings']['profiles']}
    if reference.get('profileId') not in profiles:
        raise ValueError('Unknown opening profile')
    if type(reference.get('seed')) is not int or not 0 <= reference['seed'] <= 2147483647:
        raise ValueError('Invalid seed')
    impact = reference.get('impact')
    if not isinstance(impact, list) or len(impact) != 2 or any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in impact):
        raise ValueError('Invalid impact point')
    if reference.get('state') not in ('closed', 'cracked', 'broken', 'glassRemoved'):
        raise ValueError('Invalid fracture state')
    return dict(schema=data['schema'], approved=True,
                parameters={key: parameters[key] for key in RANGES},
                reference={key: reference[key] for key in ('profileId', 'seed', 'impact', 'state')})


def load():
    return json.loads(TARGET.read_text(encoding='utf-8'))


def save(data):
    value = validate(data)
    if TARGET.exists() and load() == value:
        return value
    temporary = TARGET.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8', newline='\n')
    temporary.replace(TARGET)
    return value
