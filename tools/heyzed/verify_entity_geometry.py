"""Compare a packaged client's imported entity vertices with native source data."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path


def check_part(part, folder, catalog):
    source = json.loads((folder / catalog[part['key']]['file']).read_text(encoding='utf-8'))
    positions = [(-source['positions'][i] * 100, source['positions'][i + 2] * 100,
                  source['positions'][i + 1] * 100) for i in range(0, len(source['positions']), 3)]
    vertices = part.get('vertices')
    if not vertices:
        return {'key': part['key'], 'passed': False, 'error': 'Runtime CPU vertex data is unavailable'}
    result = dict(key=part['key'], asset=part['asset'], vertices=len(vertices),
                  position_mismatches=0, uv_mismatches=0, weight_mismatches=0,
                  missing_source_positions=0, max_position_error_cm=0)
    covered = set()
    for vertex in vertices:
        point = vertex['position']
        candidates = [i for i, source_point in enumerate(positions)
                      if math.dist(point, source_point) <= .001]
        if not candidates:
            candidates = [min(range(len(positions)), key=lambda i: math.dist(point, positions[i]))]
        distance = min(math.dist(point, positions[i]) for i in candidates)
        result['max_position_error_cm'] = max(result['max_position_error_cm'], distance)
        if distance > .001:
            result['position_mismatches'] += 1
            continue
        index = min(candidates, key=lambda i: math.dist(vertex['uv'], source['uv'][i * 2:i * 2 + 2]))
        covered.add(tuple(round(value, 3) for value in positions[index]))
        if math.dist(vertex['uv'], source['uv'][index * 2:index * 2 + 2]) > .002:
            result['uv_mismatches'] += 1
        expected = defaultdict(float)
        for influence in range(4):
            weight = source['weights'][index * 4 + influence]
            if weight > 0:
                bone = source['bones'][int(source['joints'][index * 4 + influence])]['name']
                expected[bone] += weight
        actual = {value['bone']: value['weight'] for value in vertex['influences']}
        if any(abs(expected.get(bone, 0) - actual.get(bone, 0)) > .02 for bone in expected.keys() | actual.keys()):
            result['weight_mismatches'] += 1
    used = {tuple(round(value, 3) for value in positions[index]) for index in source['indices']}
    result['missing_source_positions'] = len(used - covered)
    result['passed'] = not any(result[name] for name in
        ('position_mismatches', 'uv_mismatches', 'weight_mismatches', 'missing_source_positions'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--catalog', type=Path, default=Path(__file__).parent / '.local/entities/native/catalog.json')
    arguments = parser.parse_args()
    catalog = json.loads(arguments.catalog.read_text(encoding='utf-8'))['models']
    snapshot = json.loads(arguments.dump.read_text(encoding='utf-8-sig'))
    results = [check_part(part, arguments.catalog.parent, catalog) for part in snapshot['parts']]
    passed = bool(results) and all(result['passed'] for result in results)
    print(json.dumps(dict(passed=passed, parts=results), indent=2))
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
