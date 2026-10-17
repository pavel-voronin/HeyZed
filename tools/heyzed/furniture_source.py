"""Convert authored native-axis OBJ triangles into editor mesh input, preserving origins."""
import hashlib
import json
import math
from pathlib import Path


def within(root, path):
    path = path.resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError('Missing or escaping furniture resource: ' + str(path))
    return path


def decode(model):
    from PIL import Image
    source = Path(model['file'])
    root = Path(model['root'])
    raw = within(root, source).read_bytes()
    if hashlib.sha256(raw).hexdigest() != model['sha256']:
        raise ValueError('Furniture source changed; prepare selection again')
    positions, normals, uv, vertices, groups = [], [], [], [], {}
    definitions, dependencies = {}, {source: raw}
    current = None
    for text in raw.decode('utf-8-sig').splitlines():
        parts = text.split()
        if not parts or parts[0].startswith('#'):
            continue
        kind = parts[0]
        if kind in ('v', 'vn', 'vt'):
            count = 2 if kind == 'vt' else 3
            row = [float(x) for x in parts[1:count+1]]
            if len(row) != count or not all(math.isfinite(x) for x in row):
                raise ValueError('Invalid OBJ vector')
            (positions if kind == 'v' else normals if kind == 'vn' else uv).append(row)
        elif kind == 'mtllib':
            file = within(root, source.parent / text[7:].strip())
            content = file.read_bytes(); dependencies[file] = content
            material = None
            for line in content.decode('utf-8-sig').splitlines():
                fields = line.split()
                if not fields or fields[0].startswith('#'):
                    continue
                key = fields[0]
                if key == 'newmtl':
                    material = dict(color=[.65, .65, .65], opacity=1., roughness=.65, metallic=0.)
                    definitions[line[7:].strip()] = material
                elif material is not None:
                    if key == 'Kd': material['color'] = [float(x) for x in fields[1:4]]
                    elif key in ('d', 'Pr', 'Pm'): material[dict(d='opacity', Pr='roughness', Pm='metallic')[key]] = float(fields[1])
                    elif key == 'map_Kd':
                        image = within(root, file.parent / line[7:].strip())
                        dependencies[image] = image.read_bytes(); material['texture'] = str(image)
                    elif key not in ('Ka', 'Ks', 'Ns', 'illum'):
                        raise ValueError('Furniture material channel needs explicit support: ' + key)
        elif kind == 'usemtl':
            current = text[7:].strip()
            if current not in definitions: raise ValueError('Unknown OBJ material ' + current)
            groups.setdefault(current, [])
        elif kind == 'f':
            if len(parts) != 4 or current is None: raise ValueError('Expected authored triangulated material faces')
            face = []
            for token in parts[1:]:
                refs = token.split('/')
                def item(values, index):
                    value = int(index)
                    at = value-1 if value > 0 else len(values)+value
                    if value == 0 or not 0 <= at < len(values): raise ValueError('Invalid OBJ index')
                    return values[at]
                point = item(positions, refs[0])
                tex = item(uv, refs[1]) if len(refs)>1 and refs[1] else [0, 0]
                normal = item(normals, refs[2]) if len(refs)>2 and refs[2] else [0, 0, 0]
                face.append([*[x*100 for x in point], *normal, tex[0], 1-tex[1]])
            a, b, c = face
            ab = [b[i]-a[i] for i in range(3)]; ac = [c[i]-a[i] for i in range(3)]
            cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
            length = math.sqrt(sum(x*x for x in cross))
            if length < 1e-9: continue
            for v in face:
                if sum(n*n for n in v[3:6]) < 1e-12: v[3:6] = [n/length for n in cross]
            # Unreal's front uses cross(C-A, B-A); preserve authored outward normals.
            if sum(cross[i]*sum(v[3+i] for v in face) for i in range(3)) > 0: face[1], face[2] = face[2], face[1]
            start = len(vertices); vertices.extend(face); groups[current].extend(range(start, start+3))
    if not vertices: raise ValueError('Empty furniture mesh')
    used = list(groups)
    materials = [definitions[k] for k in used]
    for material in materials:
        material['masked'] = material['opacity'] < 1
        if material.get('texture'):
            with Image.open(material['texture']) as image:
                material['masked'] |= image.convert('RGBA').getextrema()[3][0] < 255
    digest = hashlib.sha256(Path(__file__).read_bytes())
    for path, content in sorted(dependencies.items()):
        digest.update(str(path.relative_to(root)).encode()); digest.update(content)
    return dict(vertices=vertices, groups=[groups[k] for k in used]), materials, digest.hexdigest()


def prepare(probe):
    probe = Path(probe).resolve()
    data = json.loads(probe.read_text())
    for model in data['models']:
        mesh, materials, recipe = decode(model)
        target = probe.parent / 'meshes' / (model['key']+'.json')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(mesh, separators=(',', ':')), encoding='utf-8')
        model.update(meshInput=str(target), materials=materials, recipe=recipe)
    output = probe.with_name('prepared.json')
    output.write_text(json.dumps(data, separators=(',', ':')), encoding='utf-8')
    return output


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    args = parser.parse_args()
    print(prepare(args.probe))
