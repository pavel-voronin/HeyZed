"""Compile native floor facts into shared-material polygon regions, never RGB atlases."""
from array import array
from collections import Counter, defaultdict
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import zlib

import shapely
from shapely import Polygon, MultiPolygon, box
import ground_geometry

ROOT = Path(__file__).resolve().parent
GROUND_MATERIALS = set('Road_01 Road_02 Road_03 Road_04 Road_05 Road_06 Road_07 Grass Concrete Curb Gravel Dirt Clay Dirt_Grass Burnt Sand'.split())
DETAIL_MATERIALS = GROUND_MATERIALS | {'PaintWhite', 'PaintYellow', 'PaintHidden'}


def definitions(game):
    result = {}
    for file in sorted((game/'media').glob('*.tiles.txt')):
        for match in re.finditer(r'//\s*(\S+)\s*\n\s*tile\s*\{([^}]+)\}', file.read_text('utf-8-sig')):
            properties = {a.strip(): b.strip() for a, b in re.findall(r'^\s*(\w+)\s*=([^\n]*)', match[2], re.M)}
            if '.patch.' in file.name:
                result.setdefault(match[1], {}).update(properties)
            else:
                result[match[1]] = properties
    if not result:
        raise ValueError('Installed native tile definitions are missing')
    return result


def half_plane(nx, ny, constant, threshold):
    points = [(-512, -512), (768, -512), (768, 768), (-512, 768)]
    output = []
    distance = lambda p: nx*p[0]+ny*p[1]+constant-threshold
    for a, b in zip(points, points[1:]+points[:1]):
        da, db = distance(a), distance(b)
        if da >= 0:
            output.append(a)
        if (da >= 0) != (db >= 0):
            t = da/(da-db)
            output.append((a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1])))
    return Polygon(output)


def program_id(styles):
    return 'Surface_'+hashlib.sha256('|'.join(styles).encode()).hexdigest()[:16]


def compose_near(areas, stacks, palette, catalog):
    from shapely.affinity import translate
    templates = {}
    recipes = {}
    grouped = defaultdict(list)
    affected = []
    missing = Counter()
    source_parts = 0
    unit = shapely.set_precision(box(0, 0, 1, 1), 1e-6)

    def recipe(stack_id):
        base = material_key(next(t for t in palette[stack_id] if not t['overlay'])['material'])
        layers = []
        absent = []
        pieces = 0
        for tile in palette[stack_id]:
            if not tile['overlay']:
                continue
            keys = catalog['tiles'].get(tile['name'])
            if keys is None:
                absent.append(tile['name'])
                continue
            for key in keys:
                part = catalog['meshes'][key]
                upper = part['material']
                if upper == 'PaintHidden':
                    continue
                pieces += 1
                if key not in templates:
                    v = part['vertices']
                    templates[key] = shapely.union_all([Polygon(v[i:i+3]) for i in range(0, len(v), 3)], grid_size=1e-6).intersection(unit)
                polygon = templates[key]
                nx, ny, constant, width = part['boundary']
                if width > 0:
                    core = polygon.intersection(half_plane(nx, ny, constant, width))
                    band = polygon.difference(core)
                    candidates = [(core, None), (band, (nx, ny, constant, width))]
                else:
                    candidates = [(polygon, None)]
                for cover, field in candidates:
                    cover = shapely.set_precision(cover, 1e-6)
                    if cover.area > 1e-12:
                        layers.append((cover, upper, field))
        linework = shapely.union_all([unit.boundary]+[cover.boundary for cover, _, _ in layers], grid_size=1e-6)
        faces = shapely.polygonize(shapely.get_parts(linework))
        fragments = []
        for face in faces.geoms:
            point = face.representative_point()
            if not unit.covers(point):
                continue
            styles, gates = (base,), ()
            for cover, upper, field in layers:
                if not cover.covers(point) or all(style == upper for style in styles):
                    continue
                if field is None and not upper.startswith('Paint'):
                    styles, gates = (upper,), ()
                else:
                    if len(styles) >= 4:
                        raise ValueError('More than four visible surface layers in native stack '+str(stack_id))
                    styles, gates = styles+(upper,), gates+((field if field else 1.),)
            fragments.append((face, styles, gates))
        area = sum(g.area for g, _, _ in fragments)
        if abs(area-1) > 1e-7:
            raise ValueError(f'Surface subdivision changed native tile coverage: stack={stack_id}, area={area}')
        return fragments, absent, pieces

    for cell in stacks:
        stack_id = cell['stack']
        if stack_id not in recipes:
            recipes[stack_id] = recipe(stack_id)
        fragments, absent, pieces = recipes[stack_id]
        missing.update(absent)
        source_parts += pieces
        base = material_key(next(t for t in palette[stack_id] if not t['overlay'])['material'])
        if len(fragments) == 1 and fragments[0][1] == (base,) and not fragments[0][2]:
            continue
        affected.append(box(cell['x'], cell['y'], cell['x']+1, cell['y']+1))
        for geometry, styles, gates in fragments:
            fields = tuple(tuple(round(v, 9) for v in (gate[0], gate[1], gate[2]-gate[0]*cell['x']-gate[1]*cell['y'], gate[3])) if isinstance(gate, tuple) else gate for gate in gates)
            grouped[(styles, fields)].append(translate(geometry, cell['x'], cell['y']))
    changed = shapely.union_all(affected, grid_size=1e-6)
    for material, geometry in areas.items():
        remaining = geometry.difference(changed)
        if remaining.area > 1e-12:
            grouped[((material,), ())].append(remaining)
    geometry_by_key = {}
    specifications = {}
    for spec, values in sorted(grouped.items(), key=lambda item: str(item[0])):
        geometry = shapely.union_all(values, grid_size=1e-6)
        if geometry.area <= 1e-12:
            continue
        key = hashlib.sha256(repr(spec).encode()).hexdigest()
        geometry_by_key[key] = geometry
        specifications[key] = spec
    noded = coverage(geometry_by_key, 0)
    records = []
    programs = {}
    for key, geometry in sorted(noded.items()):
        styles, gates = specifications[key]
        identity = program_id(styles)
        programs[identity] = list(styles)
        record = describe({identity: geometry})['areas'][0]
        record.update(program=list(styles), gates=gates, profile_review='pending', material_bound=all(s in DETAIL_MATERIALS for s in styles))
        records.append(record)
    expected = shapely.union_all(list(areas.values()))
    actual = shapely.union_all(list(noded.values()))
    if expected.symmetric_difference(actual).area > 1e-7:
        raise ValueError('Composed regions changed ground ownership')
    return records, programs, dict(sorted(missing.items())), source_parts


def surfaces(level):
    output = defaultdict(list)
    for record in level['areas']:
        for x, y in record['vertices']:
            gates = []
            for field in record.get('gates', ()):
                gates.append(max(0, min(1, (field[0]*x+field[1]*y+field[2])/field[3])) if isinstance(field, (tuple, list)) else field)
            gates += [0.]*(3-len(gates))
            output[record['material']].append([x, y, 0., *[round(g, 7) for g in gates]])
    return [dict(material=key, vertices=values) for key, values in sorted(output.items())]


def material_key(material):
    return 'Grass' if material.startswith('Grass') else material or 'Unresolved'


def far_material(material):
    if material.startswith('Road_') and material != 'Road_04':
        return 'Road_01'
    return dict(Gravel='Road_04', Clay='Dirt', Dirt_Grass='Dirt', Curb='Concrete').get(material, material)


def polygons(geometry):
    if geometry.is_empty:
        return []
    if isinstance(geometry, Polygon):
        return [geometry]
    return [p for p in geometry.geoms if isinstance(p, Polygon) and not p.is_empty]


def region_geometry(grid, labels):
    """Runs are input acceleration; the union removes their internal boundaries."""
    rows = defaultdict(list)
    for y in range(256):
        x = 0
        while x < 256:
            material = labels[grid[y*256+x]]
            end = x+1
            while end < 256 and labels[grid[y*256+end]] == material:
                end += 1
            if material:
                rows[material].append(box(x, y, end, y+1))
            x = end
    return {material: shapely.union_all(runs) for material, runs in sorted(rows.items())}


def coverage(areas, tolerance):
    linework = shapely.union_all([geometry.boundary for geometry in areas.values()])
    faces = shapely.polygonize(shapely.get_parts(linework))
    noded = defaultdict(list)
    source_keys = list(areas)
    spatial = shapely.STRtree([areas[key] for key in source_keys])
    for face in faces.geoms:
        point = face.representative_point()
        owners = spatial.query(point, predicate='within')
        if len(owners) > 1:
            raise ValueError('Multiple structural owners for one planar face')
        if len(owners) == 1:
            noded[source_keys[int(owners[0])]].append(face)
    areas = {key: MultiPolygon(values) for key, values in noded.items()}
    keys = sorted(areas)
    values = [areas[k] for k in keys]
    if not shapely.coverage_is_valid(values):
        raise ValueError('Native label domains do not form an edge-matched coverage')
    simplified = shapely.coverage_simplify(values, tolerance, simplify_boundary=False)
    if not shapely.coverage_is_valid(simplified):
        raise ValueError('Simplified domains are not a valid coverage')
    result = dict(zip(keys, simplified))
    for key in keys:
        if len(polygons(result[key])) != len(polygons(areas[key])):
            raise ValueError('Surface connectivity changed: '+key)
    before = shapely.union_all(values)
    after = shapely.union_all(list(result.values()))
    if before.symmetric_difference(after).area > 1e-7:
        raise ValueError('Coverage simplification changed the external boundary or its holes')
    return result


def ring(points):
    return [[round(x, 6), round(y, 6)] for x, y in list(points)[:-1]]


def describe(areas):
    records = []
    triangle_count = 0
    for material, geometry in sorted(areas.items()):
        geometry = shapely.normalize(geometry)
        contours = []
        triangles = []
        for polygon in polygons(geometry):
            contours.append(dict(outer=ring(polygon.exterior.coords), holes=[ring(h.coords) for h in polygon.interiors]))
            triangulated = shapely.constrained_delaunay_triangles(polygon)
            if abs(sum(t.area for t in triangulated.geoms)-polygon.area) > max(1e-7, polygon.area*1e-10):
                raise ValueError('Constrained triangulation does not cover its polygon: '+material)
            for triangle in triangulated.geoms:
                points = ring(triangle.exterior.coords)
                a, b, c = points
                if (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]) < 0:
                    points = [a, c, b]
                triangles.extend(points)
        triangle_count += len(triangles)//3
        records.append(dict(material=material, area_m2=geometry.area, polygons=contours, vertices=triangles,
                            material_bound=material in DETAIL_MATERIALS))
    return dict(areas=records, triangles=triangle_count,
                polygons=sum(len(r['polygons']) for r in records),
                boundary_vertices=sum(len(p['outer'])+sum(len(h) for h in p['holes']) for r in records for p in r['polygons']))


def compile_region(source, mask_path, catalog):
    started = time.monotonic()
    encoded = source.read_bytes()
    raw = zlib.decompress(encoded)
    magic, version, rx, ry, length = struct.unpack_from('>4siiiI', raw)
    if magic != b'PZBF' or version != 1:
        raise ValueError('Unsupported native floor facts')
    palette = json.loads(raw[20:20+length])
    grid = array('H', raw[20+length:])
    if sys.byteorder == 'little':
        grid.byteswap()
    if len(grid) != 65536 or any(index >= len(palette) for index in grid):
        raise ValueError('Invalid native fact grid')
    mask = mask_path.read_bytes() if mask_path.exists() else bytes(8192)
    if len(mask) != 8192:
        raise ValueError('Invalid architecture floor ownership mask')
    labels = []
    for stack in palette:
        base = next((t for t in stack if not t['overlay']), None)
        labels.append(material_key(base['material']) if base else None)
    clean = array('H', grid)
    label_names = sorted({name for name in labels if name})
    label_ids = {name: i+1 for i, name in enumerate(label_names)}
    display_palette = [None]+label_names
    label_grid = array('H', [0])*65536
    detail = []
    unresolved = Counter()
    source_materials = Counter()
    occupied = masked = 0
    for index, stack_id in enumerate(clean):
        if mask[index >> 3] & (1 << (index & 7)):
            masked += bool(palette[stack_id])
            continue
        material = labels[stack_id]
        if not material:
            continue
        occupied += 1
        label_grid[index] = label_ids[material]
        base = next(t for t in palette[stack_id] if not t['overlay'])
        source_materials[base['material']] += 1
        if material not in GROUND_MATERIALS:
            unresolved[material] += 1
        overlays = [t for t in palette[stack_id] if t['overlay']]
        if overlays:
            y, x = divmod(index, 256)
            detail.append(dict(x=x, y=y, stack=stack_id))
    areas = region_geometry(label_grid, display_palette)
    near = coverage(areas, 0)
    coarse_inputs = defaultdict(list)
    for material, geometry in areas.items():
        coarse_inputs[far_material(material)].append(geometry)
    coarse = {key: shapely.union_all(values) for key, values in coarse_inputs.items()}
    far = coverage(coarse, .5)
    horizon = coverage(coarse, 2)
    near_data, far_data, horizon_data = describe(near), describe(far), describe(horizon)
    if abs(sum(record['area_m2'] for record in near_data['areas'])-occupied) > 1e-7:
        raise ValueError('Structural coverage no longer matches the native solid-floor facts')
    composed, programs, missing_profiles, source_parts = compose_near(near, detail, palette, catalog)
    near_data['base_triangles'] = near_data['triangles']
    near_data['areas'] = composed
    near_data['triangles'] = sum(len(r['vertices'])//3 for r in composed)
    near_data['polygons'] = sum(len(r['polygons']) for r in composed)
    near_data['boundary_vertices'] = sum(len(p['outer'])+sum(len(h) for h in p['holes']) for r in composed for p in r['polygons'])
    for level in (far_data, horizon_data):
        for record in level['areas']:
            styles = (record['material'],)
            identity = program_id(styles)
            programs[identity] = list(styles)
            record.update(material=identity, program=list(styles), gates=[])
    levels = [dict(name='near', tolerance_m=0, **near_data),
              dict(name='far', tolerance_m=.5, **far_data),
              dict(name='horizon', tolerance_m=2, **horizon_data)]
    for level in levels:
        level['surfaces'] = surfaces(level)
    profile_names = {tile['name'] for cell in detail for tile in palette[cell['stack']] if tile['overlay']}
    bindings = {name: catalog['tiles'][name] for name in sorted(profile_names) if name in catalog['tiles']}
    return dict(schema=1, representation='structural-ground-regions', region=[rx, ry], extent_m=256, coordinate_precision_m=1e-6,
                source_sha256=hashlib.sha256(encoded).hexdigest(),
                architecture_mask_sha256=hashlib.sha256(mask).hexdigest(),
                source_palette=palette, label_palette=display_palette, detail_stacks=detail,
                source_material_counts=dict(sorted(source_materials.items())),
                unresolved_material_counts=dict(sorted(unresolved.items())),
                occupied_tiles=occupied, masked_tiles=masked,
                levels=levels, surface_programs=programs, detail_source_parts=source_parts, detail_profile_bindings=bindings, unresolved_profile_counts=missing_profiles,
                detail_status='Explicit native-XY profiles remain visually unapproved',
                prepare_seconds=time.monotonic()-started), label_grid


def main():
    process_started = time.monotonic()
    parser = argparse.ArgumentParser()
    parser.add_argument('--region', default='31_45')
    parser.add_argument('--game', type=Path, default=Path(os.environ.get('PZ_GAME_DIR', r'C:\Program Files (x86)\Steam\steamapps\common\ProjectZomboid')))
    args = parser.parse_args()
    rx, ry = map(int, args.region.split('_'))
    key = f'{rx}_{ry}'
    world = json.loads((ROOT/'.local/world/current.json').read_text('utf-8'))
    source = Path(world['folder'])/(key+'.pzf')
    mask = ROOT/'.local/architecture-world/resolved/floor-masks'/(key+'.bin')
    catalog = ground_geometry.compile_catalog(definitions(args.game))
    data, grid = compile_region(source, mask, catalog)
    elapsed = data.pop('prepare_seconds')
    data['world_identity'] = world['identity']
    used = {key for keys in data['detail_profile_bindings'].values() for key in keys}
    dependencies = {key: catalog['meshes'][key] for key in sorted(used)}
    data['compiler_sha256'] = hashlib.sha256(Path(__file__).read_bytes()+json.dumps(dependencies, sort_keys=True).encode()).hexdigest()
    encoded = json.dumps(data, separators=(',', ':')).encode()
    digest = hashlib.sha256(encoded).hexdigest()[:16]
    folder = ROOT/'.local/ground-structure'/(key+'-'+digest)
    folder.mkdir(parents=True, exist_ok=True)
    payload = folder/'region.json'
    payload.write_bytes(encoded)
    compact = dict(data)
    compact.pop('levels')
    compact['areas'] = [{k: v for k, v in area.items() if k != 'vertices'} for area in data['levels'][0]['areas']]
    compact['lod_rules'] = [dict(name='far', tolerance_m=.5), dict(name='horizon', tolerance_m=2)]
    compact['far_material_remap'] = {material: far_material(material) for material in sorted({style for styles in data['surface_programs'].values() for style in styles})}
    structure = zlib.compress(json.dumps(compact, separators=(',', ':')).encode(), 9)
    (folder/'structure.json.z').write_bytes(structure)
    if sys.byteorder == 'little':
        grid.byteswap()
    grid_bytes = zlib.compress(grid.tobytes(), 9)
    (folder/'labels.grid.z').write_bytes(grid_bytes)
    manifest = dict(region=key, file=str(payload), identity=world['identity'],
                    source=source.as_posix(), structural_bytes=len(structure), label_grid_bytes=len(grid_bytes),
                    compiled_json_bytes=len(encoded), prepare_seconds=elapsed,
                    levels=[{k: level[k] for k in ('name', 'triangles', 'polygons', 'boundary_vertices')} for level in data['levels']],
                    unresolved_material_counts=data['unresolved_material_counts'],
                    unresolved_profile_counts=data['unresolved_profile_counts'],
                    detail_source_parts=data['detail_source_parts'],
                    near_base_triangles=data['levels'][0]['base_triangles'],
                    surface_programs=len(data['surface_programs']))
    manifest['total_prepare_seconds'] = time.monotonic()-process_started
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        class MemoryCounters(ctypes.Structure):
            _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD)]+[(name, ctypes.c_size_t) for name in ('PeakWorkingSetSize', 'WorkingSetSize', 'QuotaPeakPagedPoolUsage', 'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage', 'QuotaNonPagedPoolUsage', 'PagefileUsage', 'PeakPagefileUsage')]
        counters = MemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        ctypes.windll.kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        ctypes.windll.psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
        if not ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.windll.kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
            raise ctypes.WinError()
        manifest.update(prepare_peak_private_bytes=counters.PeakPagefileUsage, prepare_peak_physical_bytes=counters.PeakWorkingSetSize)
    (ROOT/'.local/ground-structure/current.json').write_text(json.dumps(manifest, indent=2), 'utf-8')
    print(json.dumps({k:v for k,v in manifest.items() if k != 'unresolved_profile_counts'}), flush=True)


if __name__ == '__main__':
    main()
