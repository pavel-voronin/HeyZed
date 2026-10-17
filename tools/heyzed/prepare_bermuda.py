"""Download CC0 Bermuda sources and assemble one reusable native lawn patch."""
import hashlib
import json
from pathlib import Path
import random
import struct
import urllib.request
import math

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT/'vegetation/bermuda'


def download():
    SOURCE.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request('https://api.polyhaven.com/files/grass_bermuda_01', headers={'User-Agent': 'heyzed local asset preparation'})
    with urllib.request.urlopen(request) as response:
        catalog = json.load(response)
    model = catalog['gltf']['2k']['gltf']
    entries = {'grass_bermuda_01.gltf': model}
    for name, record in model['include'].items():
        entries[name] = record
    entries['textures/grass_bermuda_01_alpha_2k.png'] = catalog['Alpha']['2k']['png']
    records = {}
    for name, record in entries.items():
        path = SOURCE/name; path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or hashlib.md5(path.read_bytes()).hexdigest() != record['md5']:
            with urllib.request.urlopen(record['url']) as response:
                path.write_bytes(response.read())
        data = path.read_bytes()
        if hashlib.md5(data).hexdigest() != record['md5']:
            raise RuntimeError('Bermuda source checksum mismatch: '+name)
        records[name] = dict(url=record['url'], sha256=hashlib.sha256(data).hexdigest())
    manifest = dict(asset='Grass Bermuda 01', source='https://polyhaven.com/a/grass_bermuda_01',
                    license='CC0-1.0', licenseUrl='https://polyhaven.com/license',
                    authors=['Rico Cilliers', 'Rob Tuytel'], files=records)
    (SOURCE/'source.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')


def prepare():
    manifest = json.loads((SOURCE/'source.json').read_text())
    for name, record in manifest['files'].items():
        if hashlib.sha256((SOURCE/name).read_bytes()).hexdigest() != record['sha256']:
            raise RuntimeError('Bermuda source differs: '+name)
    document = json.loads((SOURCE/'grass_bermuda_01.gltf').read_text())
    buffers = [(SOURCE/b['uri']).read_bytes() for b in document['buffers']]

    def accessor(index):
        record = document['accessors'][index]; view = document['bufferViews'][record['bufferView']]
        count = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[record['type']]
        code = {5123: 'H', 5125: 'I', 5126: 'f'}[record['componentType']]
        row = struct.Struct('<'+code*count)
        offset = view.get('byteOffset', 0)+record.get('byteOffset', 0)
        stride = view.get('byteStride', row.size)
        return [row.unpack_from(buffers[view['buffer']], offset+i*stride) for i in range(record['count'])]

    plants = []
    for node in document['nodes']:
        if not any('_'+kind+'_' in node['name'] for kind in ('small', 'medium')):
            continue
        primitive = document['meshes'][node['mesh']]['primitives'][0]
        attributes = primitive['attributes']
        plants.append(dict(name=node['name'], positions=accessor(attributes['POSITION']),
                           normals=accessor(attributes['NORMAL']), uv=accessor(attributes['TEXCOORD_0']),
                           indices=[v[0] for v in accessor(primitive['indices'])]))
    rng = random.Random(41823); vertices = []; indices = []
    # A jittered 8x6 patch replaces per-blade instances. Node translations are the
    # source display layout; local mesh coordinates are each reusable plant.
    for i in range(48):
        plant = rng.choice(plants)
        angle = rng.random()*math.tau; ca, sa = math.cos(angle), math.sin(angle)
        x = ((i%8+rng.uniform(.15, .85))/8-.5)*.34
        y = ((i//8+rng.uniform(.15, .85))/6-.5)*.34
        scale = rng.uniform(.85, 1.1)
        first = len(vertices)
        for position, normal, uv in zip(plant['positions'], plant['normals'], plant['uv']):
            px, py, pz = position; nx, ny, nz = normal
            # glTF Y-up to Unreal Z-up, metres to centimetres. The reflection
            # gives Unreal's winding; authored UVs and leaf silhouettes remain.
            vertices.append([round((x+(px*ca-pz*sa)*scale)*100, 6),
                             round((y+(px*sa+pz*ca)*scale)*100, 6), round(py*scale*100, 6),
                             round(nx*ca-nz*sa, 6), round(nx*sa+nz*ca, 6), ny, uv[0], uv[1]])
        indices.extend(first+index for index in plant['indices'])
    target = ROOT/'.local/bermuda-lawn.json'; target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(dict(vertices=vertices, indices=indices, plants=48,
                                     source=manifest['source']), separators=(',', ':')))
    print(f'Prepared Bermuda lawn patch: {len(vertices)} vertices, {len(indices)//3} triangles, 48 source plants')


if __name__ == '__main__':
    download()
    prepare()
