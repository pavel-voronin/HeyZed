"""Acquire CC0 sources and retain compact, reusable tree and shrub resources."""
from pathlib import Path
import concurrent.futures
import gzip
import hashlib
import json
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT/'vegetation/woody'
CACHE = ROOT/'.local/vegetation-sources'
ASSETS = {'tree_small_02': ('Tree', 'Rico Cilliers'), 'shrub_04': ('Shrub', 'Rico Cilliers'),
          'pine_tree_01': ('Pine', 'Rico Cilliers')}


def fetch(url, path, md5=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or md5 and hashlib.md5(path.read_bytes()).hexdigest() != md5:
        request = urllib.request.Request(url, headers={'User-Agent': 'heyzed local asset preparation'})
        with urllib.request.urlopen(request) as response:
            path.write_bytes(response.read())
    if md5 and hashlib.md5(path.read_bytes()).hexdigest() != md5:
        raise RuntimeError('Vegetation source differs: '+str(path))


def download():
    provenance = {}
    for asset, (kind, author) in ASSETS.items():
        catalog_path = CACHE/asset/'catalog.json'
        fetch('https://api.polyhaven.com/files/'+asset, catalog_path)
        catalog = json.loads(catalog_path.read_text())
        model = catalog['blend']['1k']['blend'] if kind != 'Shrub' else catalog['gltf']['2k']['gltf']
        originals = {asset+('.blend' if kind != 'Shrub' else '.gltf'): model}
        if kind == 'Shrub':
            originals.update(model['include'])
        for name, record in originals.items():
            fetch(record['url'], CACHE/asset/name, record['md5'])
        maps = {'tree_small_02': ['Diffuse', 'nor_gl', 'arm', 'branch_diff', 'branch_nor_gl', 'branch_arm',
                                 'leaves_diff', 'leaves_nor_gl', 'leaves_arm', 'leaves_alpha'],
                'shrub_04': ['Diffuse', 'nor_gl', 'arm', 'Alpha'],
                'pine_tree_01': ['bark_diff','bark_nor_gl','bark_arm','trunk_a_diff','trunk_a_nor_gl','trunk_a_arm',
                                 'twig_diff','twig_nor_gl','twig_arm','twig_alpha','trunk_b_diff','trunk_b_nor_gl','trunk_b_arm']}[asset]
        records = {}
        jobs = []
        for name in maps:
            record = catalog[name]['2k']['png' if 'alpha' in name.lower() else 'jpg']
            filename = record['url'].rsplit('/', 1)[-1]
            jobs.append((record, SOURCE/'textures'/filename))
        def acquire(job):
            record, path = job; fetch(record['url'], path, record['md5'])
            return path.name, dict(url=record['url'], sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            records.update(pool.map(acquire, jobs))
        provenance[asset] = dict(source='https://polyhaven.com/a/'+asset, author=author, license='CC0-1.0',
                                 licenseUrl='https://polyhaven.com/license', originalModel=dict(url=model['url'], md5=model['md5']),
                                 textures=records, geometry=kind+'.json.gz')
    (SOURCE/'sources.json').write_text(json.dumps(provenance, indent=2)+'\n')


def expand():
    folder = ROOT/'.local/woody'; folder.mkdir(parents=True, exist_ok=True)
    manifest=json.loads((SOURCE/'sources.json').read_text())
    for kind,source in [(kind,name) for name,(kind,author) in ASSETS.items()]:
        data=(SOURCE/(kind+'.json.gz')).read_bytes()
        record=manifest[source]
        if hashlib.sha256(data).hexdigest()!=record['geometrySha256']:raise RuntimeError('Prepared vegetation geometry differs: '+kind)
        for filename,texture in record['textures'].items():
            if hashlib.sha256((SOURCE/'textures'/filename).read_bytes()).hexdigest()!=texture['sha256']:raise RuntimeError('Vegetation texture differs: '+filename)
        (folder/(kind+'.json')).write_bytes(gzip.decompress(data))


if __name__ == '__main__':
    download()
    subprocess.run(['C:/Program Files/Blender Foundation/Blender 5.1/blender.exe', '-b',
                    '--python', str(ROOT/'prepare_woody_meshes.py')], check=True)
    expand()
