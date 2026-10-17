"""Prepare addressed construction facts for one native building; no game images."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent/'roof-lab'))
from pzdata import header, read_chunk
sys.path.insert(0, str(ROOT.parent/'laboratory'))
from geometry.profile import resolve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--building', default='31_45_25')
    args = parser.parse_args()
    native = Path('C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid/media/maps/Muldraugh, KY')
    cx, cy, _ = map(int, args.building.split('_'))
    h = header(native/f'{cx}_{cy}.lotheader')
    building = next(b for b in h['buildings'] if b['id'] == args.building)
    levels = [r['z'] for r in building['rooms'] if r['name'] != 'emptyoutside']
    maximum = max(levels)
    inside = {(x,y,r['z']) for r in building['rooms'] if r['z'] <= maximum
              for a,b,w,d in r['rects'] for x in range(a,a+w) for y in range(b,b+d)}
    props = {r['name']:r.get('properties',{}) for r in json.loads((ROOT.parent/'resources-lab/.local/catalog.json').read_text())['rows'] if r['kind']=='tile'}
    x0,y0,x1,y1 = building['bounds']
    packs, headers, cells = {}, {}, []
    for wx in range((x0-1)//8, (x1+1)//8+1):
        for wy in range((y0-1)//8, (y1+1)//8+1):
            region = wx//32,wy//32
            if region not in packs:
                rx,ry=region
                packs[region]=(native/f'world_{rx}_{ry}.lotpack').read_bytes()
                headers[region]=header(native/f'{rx}_{ry}.lotheader')
            for x,y,z,room,stack in read_chunk(packs[region],headers[region],wx,wy):
                if not (0<=z<=maximum and x0-1<=x<=x1+1 and y0-1<=y<=y1+1):continue
                walls={}
                floor=None
                stairs=None
                for name in stack:
                    p=props.get(name,{})
                    if name.startswith(("roofs_","rooftop_")) or "_roofs_" in name:continue
                    if (x,y,z) in inside and 'solidfloor' in p and not name.startswith(('roofs_','rooftop_')):
                        floor=name
                    for axis,neighbor in [('N',(x,y-1,z)),('W',(x-1,y,z))]:
                        if (x,y,z) not in inside and neighbor not in inside:continue
                        wall = 'Wall'+axis in p or 'WallNW' in p or 'DoorWall'+axis in p or 'Window'+axis in p
                        opening = 'door' if 'DoorWall'+axis in p or 'door'+axis in p else 'window' if 'Window'+axis in p or 'window'+axis in p else ''
                        if wall or opening:
                            item=walls.setdefault(axis,dict(axis=axis,sources=[],opening='',door=False,window=False))
                            item['sources'].append(name)
                            if wall or 'wall' in p:item['wall']=True
                            if opening:item['opening']=opening
                            if 'door'+axis in p:item['door']=True;item['doorSource']=name;item['doorGlass']=p.get('Material2')=='Glass' or p.get('MaterialType')=='Glass'
                            if 'window'+axis in p:item['window']=True
                            if 'MaterialType' in p:item['material']=p['MaterialType']
                            if 'WallNW' in p:item['cornerSource']=p.get('CornerNorthWall' if axis=='N' else 'CornerWestWall',name)
                    if (x,y,z) in inside:
                        for axis in ('N','W'):
                            for part,offset in [('B',0),('M',1),('T',2)]:
                                if 'stairs'+part+axis in p:stairs=dict(axis=axis,part=offset,source=name)
                if floor or walls or stairs:
                    for axis,wall in walls.items():
                        wall['openingType']='window.storefront' if wall['opening']=='window' and any(n.startswith(('walls_commercial_01_','walls_commercial_02_')) for n in wall['sources']) else wall['opening']+'.standard' if wall['opening'] else ''
                        a=(x,y-1,z) if axis=='N' else (x,y,z)
                        b=(x,y,z) if axis=='N' else (x-1,y,z)
                        wall['surfaceA']='Plaster' if a in inside else 'Brick'
                        wall['surfaceB']='Plaster' if b in inside else 'Brick'
                    cells.append(dict(address=[x,y,z],floor=floor,walls=[walls[a] for a in ('N','W') if a in walls],stairs=stairs))
    metric=resolve()
    floor_cm=metric['floorHeight']*100
    generated={ROOT/'client/Source/HeyZed/HeyZedMetrics.h':
        '#pragma once\n\n// Generated from tools/laboratory/geometry/profile.py; standing avatar contract: NativeMovement.java.\nnamespace HeyZedMetrics\n{\n    inline constexpr double Tile=100;\n    inline constexpr double WallThickness='+repr(metric['wallThickness']*100)+';\n    inline constexpr double Floor='+repr(floor_cm)+';\n    inline constexpr double StandingHeight=170;\n}\n',
        ROOT/'adapter/Contents/mods/HeyZed/42.20.4/java/src/com/pavelvoronin/heyzed/UnrealMetrics.java':
        'package com.pavelvoronin.heyzed;\n\nfinal class UnrealMetrics {\n    static final double FLOOR_CM='+repr(floor_cm)+';\n    private UnrealMetrics(){}\n}\n'}
    for path,text in generated.items():
        if not path.exists() or path.read_text()!=text:path.write_text(text,encoding='utf-8')
    result=dict(schema=1,id=args.building,bounds=building['bounds'],levels=maximum+1,
                label=building['label'],metric=metric,cells=cells,footprint=sorted(inside),
                source_sha256=hashlib.sha256((native/f'{cx}_{cy}.lotheader').read_bytes()+packs[(cx,cy)]).hexdigest())
    folder=ROOT/'.local/architecture';folder.mkdir(parents=True,exist_ok=True)
    (folder/'building.json').write_text(json.dumps(result,separators=(',',':')),encoding='utf-8')
    bridge=Path.home()/'.heyzed/architecture';bridge.mkdir(parents=True,exist_ok=True)
    pending=bridge/'current.pending'
    pending.write_text(json.dumps(dict(path=str((folder/'building.json').resolve()))),encoding='utf-8');pending.replace(bridge/'current.json')
    print(json.dumps(dict(id=result['id'],bounds=result['bounds'],levels=result['levels'],cells=len(cells),
                          walls=sum(len(c['walls']) for c in cells),stairs=sum(bool(c['stairs']) for c in cells))))


if __name__=='__main__':main()
