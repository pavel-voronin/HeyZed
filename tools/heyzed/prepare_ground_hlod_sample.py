"""Prepare one bounded native ground authoring input, without editing the map."""
import argparse
from array import array
import hashlib
import json
from pathlib import Path
import sys
import zlib

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--region', default='31_45')
    args = parser.parse_args()
    x, y = map(int, args.region.split('_'))
    key = f'{x}_{y}'
    current = json.loads((ROOT / '.local/authored-regions/current.json').read_text('utf-8'))
    source = Path(current['folder']) / (key + '.json')
    document = json.loads(source.read_text('utf-8'))
    grid = array('H', zlib.decompress(source.with_suffix('.grid').read_bytes()))
    if sys.byteorder == 'big':
        grid.byteswap()
    if len(grid) != 65536 or document['schema'] != 2:
        raise ValueError('Invalid prepared ground region')
    mask_path = ROOT / '.local/architecture-world/resolved/floor-masks' / (key + '.bin')
    removed = 0
    if mask_path.exists():
        mask = mask_path.read_bytes()
        if len(mask) != 8192:
            raise ValueError('Invalid architecture floor mask')
        for i in range(65536):
            if mask[i >> 3] & (1 << (i & 7)):
                removed += bool(grid[i])
                grid[i] = 0
    native = grid.tobytes()
    authoring = hashlib.sha256()
    for relative in ('client/Source/HeyZedAuthoring/HeyZedGroundHLODAuthor.cpp',
                     'client/Source/HeyZedAuthoring/HeyZedGroundHLODAuthor.h',
                     'author_ground_hlod_sample.py', 'prepare_ground_hlod_preview.py', 'prepare_ground_hlod_sample.py'):
        authoring.update((ROOT/relative).read_bytes())
    authoring_sha256 = authoring.hexdigest()
    digest = hashlib.sha256(source.read_bytes()+native+authoring_sha256.encode()).hexdigest()[:16]
    folder = ROOT / '.local/ground-hlod-sample' / (key+'-'+digest)
    folder.mkdir(parents=True, exist_ok=True)
    recipe = folder / (key+'.json')
    recipe.write_text(json.dumps(document), 'utf-8')
    if sys.byteorder == 'big':
        grid.byteswap()
    recipe.with_suffix('.grid').write_bytes(zlib.compress(grid.tobytes(), 6))
    manifest = dict(region=key, identity=current['identity'], source_recipe=current['recipe'], authoring_sha256=authoring_sha256,
                    recipe=str(recipe), report=str(folder/'result.json'),
                    package='/Game/HeyZed/GroundHLODSamples/Region_'+key+'_'+digest,
                    occupied_tiles=sum(bool(i) for i in grid), architecture_tiles_removed=removed,
                    input_sha256=hashlib.sha256(recipe.read_bytes()+recipe.with_suffix('.grid').read_bytes()).hexdigest())
    (folder/'input.json').write_text(json.dumps(manifest, indent=2), 'utf-8')
    (ROOT/'.local/ground-hlod-sample/current.json').write_text(json.dumps(manifest, indent=2), 'utf-8')
    print(json.dumps(manifest), flush=True)


if __name__ == '__main__':
    main()
