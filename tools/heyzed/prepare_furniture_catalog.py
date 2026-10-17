"""Prepare reusable furniture resources without reading any world or save placement."""
from pathlib import Path
import argparse
import hashlib
import json
from prepare_furniture_probe import bindings, within, DEFAULT_PACK, ROOT


def furniture_domain(tile, props):
    return not (tile.startswith(('vegetation_', 'blends_', 'e_', 'f_bushes_', 'floors_exterior_', 'street_', 'industry_railroad_'))
                or any(k in props for k in ('tree', 'Bush', 'grass', 'water')))


def prepare(folder, pack, properties=None):
    if properties is None:
        catalog = json.loads((ROOT.parent/'resources-lab/.local/catalog.json').read_text())
        properties = {r['name']: r.get('properties', {}) for r in catalog['rows'] if r['kind'] == 'tile'}
    selected, rejected = bindings(pack, properties)
    ambiguous = {tile for tile, rows in selected.items() if len({b['card'] for b in rows}) != 1}
    selected = {tile: rows for tile, rows in selected.items() if tile not in ambiguous and furniture_domain(tile, properties[tile])}
    models, runtime = {}, {}
    for tile, rows in selected.items():
        runtime[tile] = []
        for b in rows:
            if not b['allowRepack']: raise ValueError('Pack forbids cooking '+b['card'])
            key = b['card'].replace('-', '')+'_'+b['variant']
            if key not in models:
                source = within(Path(b['root']), Path(b['file']).relative_to(b['root']))
                models[key] = dict(key=key, file=str(source), root=b['root'], author=b['author'], url=b['url'], sha256=hashlib.sha256(source.read_bytes()).hexdigest())
            runtime[tile].append(dict(mesh=key, state=b['state'], placement=b['placement'], parts=b['parts'], emitter=b['emitter']))
    data = dict(schema=3, bindings=runtime, models=list(models.values()), pack=str(pack),
                packSha256=hashlib.sha256(pack.read_bytes()).hexdigest(), ambiguous=sorted(ambiguous), rejected=dict(rejected))
    folder.mkdir(parents=True, exist_ok=True)
    target = folder/'catalog.json'; target.write_text(json.dumps(data, separators=(',', ':')), encoding='utf-8')
    print(json.dumps(dict(tiles=len(runtime), models=len(models), output=str(target))), flush=True)
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'.local/furniture-catalog')
    parser.add_argument('--pack', type=Path, default=DEFAULT_PACK)
    args = parser.parse_args(); prepare(args.output, args.pack)
