"""Prepare a bounded structural calibration and audit native base-region compaction."""
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parent
BRIDGE = ROOT.parent


def boundary_points(recipe, detail):
    boundary = recipe['boundary']
    arc = boundary['arc']
    n = math.ceil((arc['endDegrees']-arc['startDegrees'])/detail['arcStepDegrees'])
    points, tangents = [boundary['start']], [[1, 0]]
    for i in range(n+1):
        a = math.radians(arc['startDegrees']+(arc['endDegrees']-arc['startDegrees'])*i/n)
        points.append([arc['center'][0]+arc['radius']*math.cos(a), arc['center'][1]+arc['radius']*math.sin(a)])
        tangents.append([-math.sin(a), math.cos(a)])
    points.append(boundary['end'])
    tangents.append([0, 1])
    return points, tangents


def contours(cells):
    edges = set()
    for x,y in cells:
        for neighbor,a,b in [((x,y-1),(x,y),(x+1,y)),((x+1,y),(x+1,y),(x+1,y+1)),((x,y+1),(x+1,y+1),(x,y+1)),((x-1,y),(x,y+1),(x,y))]:
            if neighbor not in cells:
                edges.add((a,b))
    outgoing = defaultdict(set)
    for a,b in edges:
        outgoing[a].add(b)
    loops = []
    while edges:
        first = min(edges)
        a,b = first
        loop = [a]
        while True:
            edges.remove((a,b));outgoing[a].remove(b)
            if b == loop[0]:
                break
            loop.append(b)
            dx,dy = b[0]-a[0],b[1]-a[1]
            options = outgoing[b]
            if not options:
                raise ValueError('Open native area contour')
            c = max(options, key=lambda c: math.atan2(dx*(c[1]-b[1])-dy*(c[0]-b[0]), dx*(c[0]-b[0])+dy*(c[1]-b[1])))
            a,b = b,c
        simple = [p for a,p,b in zip(loop[-1:]+loop[:-1],loop,loop[1:]+loop[:1]) if (p[0]-a[0])*(b[1]-p[1]) != (p[1]-a[1])*(b[0]-p[0])]
        loops.append(simple)
    return loops


def native_audit(output):
    current = json.loads((BRIDGE/'.local/world/current.json').read_text('utf-8'))
    x0,y0,size = 8096,11664,32
    cache = {}
    palette, ids, grid, groups, unresolved = [], {}, [], defaultdict(set), defaultdict(int)
    for y in range(size):
        for x in range(size):
            wx,wy = x0+x,y0+y
            key = (wx//256,wy//256)
            if key not in cache:
                raw = zlib.decompress((Path(current['folder'])/f'{key[0]}_{key[1]}.pzf').read_bytes())
                magic,version,rx,ry,length = struct.unpack_from('>4siiiI',raw)
                if (magic,version,rx,ry) != (b'PZBF',1,*key):
                    raise ValueError('Unsupported floor facts')
                cache[key] = json.loads(raw[20:20+length]),raw[20+length:]
            stacks,indices = cache[key]
            index = struct.unpack_from('>H',indices,((wy%256)*256+wx%256)*2)[0]
            stack = stacks[index]
            identity = json.dumps(stack,sort_keys=True,separators=(',',':'))
            if identity not in ids:
                ids[identity]=len(palette);palette.append(stack)
            grid.append(ids[identity])
            base = next((t['material'] or 'Unknown' for t in stack if not t['overlay']), 'No floor')
            groups[base].add((x,y))
            for t in stack:
                if t['overlay']:
                    unresolved[t['name']] += 1
    areas = [dict(nativeMaterial=material, contours=contours(cells)) for material,cells in sorted(groups.items())]
    # Reverse rasterization establishes what this base-only representation preserves.
    def covers(point,poly):
        px,py=point;inside=False
        for a,b in zip(poly,poly[1:]+poly[:1]):
            if (a[1]>py)!=(b[1]>py) and px<(b[0]-a[0])*(py-a[1])/(b[1]-a[1])+a[0]:
                inside=not inside
        return inside
    mismatches = 0
    for material,cells in groups.items():
        loops = next(a['contours'] for a in areas if a['nativeMaterial']==material)
        recovered = {(x,y) for y in range(size) for x in range(size) if sum(covers((x+.5,y+.5),p) for p in loops)%2}
        mismatches += len(cells^recovered)
    if mismatches:
        raise ValueError('Native base contour round trip failed')
    baseline = dict(palette=palette,grid=grid)
    structural = dict(baseAreas=areas,palette=palette,overlayAddresses=[[i,v] for i,v in enumerate(grid) if any(t['overlay'] for t in palette[v])])
    encode=lambda value:json.dumps(value,separators=(',',':')).encode()
    (output/'native-structure.json').write_bytes(encode(structural))
    return dict(origin=[x0,y0],size=size,worldIdentity=current['identity'],nativeCells=size*size,baseMaterials={k:len(v) for k,v in sorted(groups.items())},baseContours=sum(len(a['contours']) for a in areas),contourVertices=sum(len(p) for a in areas for p in a['contours']),baseRoundTripMismatches=mismatches,unresolvedOverlayTypes=len(unresolved),unresolvedOverlayOccurrences=sum(unresolved.values()),gridPaletteBytes=len(encode(baseline)),gridPaletteZlibBytes=len(zlib.compress(encode(baseline))),structuralBytes=len(encode(structural)),structuralZlibBytes=len(zlib.compress(encode(structural))),limitation='Only base material occupancy is recovered. Overlay positions and original palette are preserved; sub-tile shapes and road/field/forest semantics are unresolved, not inferred from color or material.')


def main():
    output = BRIDGE/'.local/structural-ground'
    output.mkdir(parents=True,exist_ok=True)
    recipe = json.loads((ROOT/'calibration.json').read_text('utf-8'))
    report = dict(calibrationSha256=hashlib.sha256((ROOT/'calibration.json').read_bytes()).hexdigest(),calibrationBytes=len(json.dumps(recipe,separators=(',',':')).encode()),native=native_audit(output))
    (output/'preparation.json').write_text(json.dumps(report,indent=2),'utf-8')
    project = output/'probe/ground-structure.uproject'
    project.parent.mkdir(parents=True,exist_ok=True)
    project.write_text(json.dumps(dict(FileVersion=3,EngineAssociation='5.8',Plugins=[dict(Name=n,Enabled=True) for n in ('PythonScriptPlugin','EditorScriptingUtilities','GeometryScripting')]),indent=2),'utf-8')
    config = project.parent/'Config/DefaultEngine.ini'
    config.parent.mkdir(exist_ok=True)
    config.write_text('[/Script/Engine.RendererSettings]\nr.AllowStaticLighting=False\nr.GenerateMeshDistanceFields=False\n','utf-8')
    print(json.dumps(report,separators=(',',':')))


if __name__=='__main__':
    main()
