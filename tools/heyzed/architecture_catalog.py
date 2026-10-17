"""Native construction inventory and addressed source snapshots for the UE bake."""
from collections import Counter, OrderedDict, defaultdict
from functools import lru_cache
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent
sys.path[:0] = [str(ROOT.parent/'roof-lab'), str(ROOT.parent/'window-lab'), str(ROOT.parent/'laboratory')]
from pzdata import header, read_chunk
from geometry.profile import resolve
from geometry.attachments import native_bindings
from archetype_workbench import workbench, resolve_source, load_assignments

NATIVE = Path('C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid/media/maps/Muldraugh, KY')
OUTPUT = ROOT/'.local/architecture-world'


def encoded(value):
    return json.dumps(value, separators=(',', ':'), sort_keys=True).encode('utf-8')


def publish(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix+'.pending')
    content = encoded(value)
    temporary.write_bytes(gzip.compress(content, mtime=0) if path.suffix == '.gz' else content)
    temporary.replace(path)


class NativeConstructionSource:
    def __init__(self):
        catalogue = json.loads((ROOT.parent/'resources-lab/.local/catalog.json').read_text('utf-8'))
        self.properties = {r['name']:r.get('properties', {}) for r in catalogue['rows'] if r['kind']=='tile'}
        self.book = json.loads((ROOT.parent/'materials-lab/materials.json').read_text('utf-8'))
        self.workbench = workbench(False)
        bindings=json.loads((ROOT.parent/'laboratory/geometry/archetype-bindings.json').read_text('utf-8'))
        assignments=load_assignments()
        bindings=dict(bindings,**{key:{**bindings.get(key,{}),**assignments.get(key,{})} for key in ('types','materials','openings')})
        self.sources={}
        for name,p in self.properties.items():
            side=next((axis for axis in ('N','W') if any(key+axis in p for key in ('Wall','DoorWall','Window','door','window','cut','doorFr'))),None)
            states={}
            sheet,_,number=name.rpartition('_')
            if number.isdigit():
                for key,state in [('OpenTileOffset','open'),('SmashedTileOffset','broken'),('GlassRemovedOffset','glassRemoved')]:
                    if key in p:states[state]=sheet+'_'+str(int(number)+int(p[key]))
            tile=dict(name=name,properties=p,orientation=side,states=states)
            resolved=resolve_source(tile,self.workbench['registry'],bindings,self.book)
            if resolved:self.sources[name]=resolved
        for source in list(self.sources.values()):
            for state, name in source.get('nativeStates', {}).items():
                if state=='closed':continue
                self.sources[name]=dict(source,name=name,state=state,base=source['name'],
                                        nativeEvidence=dict(source.get('nativeEvidence',{}),spriteState=state))
        self.attachments = native_bindings()
        self.header_cache = {}
        self.pack_key = None
        self.pack = None
        self.chunks = OrderedDict()

    def definitions(self,revision):
        attachments={name:dict(family=binding['family']['id'],category=binding['family']['category'],direction=binding['direction'],
                               **{k:v for k,v in binding.items() if k not in ('family','direction')})
                     for name,binding in self.attachments.items()}
        return dict(registry=self.workbench['registry'],geometry=self.workbench['geometry'],sources=self.sources,
                    materials=self.book['materials'],attachmentBindings=attachments,revision=revision)

    def region_header(self, rx, ry):
        key = rx, ry
        if key not in self.header_cache:
            self.header_cache[key] = header(NATIVE/f'{rx}_{ry}.lotheader')
        return self.header_cache[key]

    def chunk(self, wx, wy):
        key = wx, wy
        if key in self.chunks:
            self.chunks.move_to_end(key)
            return self.chunks[key]
        region = wx//32, wy//32
        if self.pack_key != region:
            self.pack = (NATIVE/f'world_{region[0]}_{region[1]}.lotpack').read_bytes()
            self.pack_key = region
        values = list(read_chunk(self.pack, self.region_header(*region), wx, wy))
        self.chunks[key] = values
        if len(self.chunks)>96:
            self.chunks.popitem(last=False)
        return values

    def snapshot(self, building):
        rooms = building['rooms']
        occupied = [r['z'] for r in rooms if r['name']!='emptyoutside']
        if not occupied:
            return None
        lower, upper = min(occupied), max(occupied)
        room_at = {(x,y,r['z']):str(i) for i,r in enumerate(rooms) if r['z']<=upper
                   for a,b,w,d in r['rects'] for x in range(a,a+w) for y in range(b,b+d)}
        inside = set(room_at)
        x0,y0,x1,y1 = building['bounds']
        cells = {}
        tiles = []
        room_finishes = defaultdict(Counter)
        exterior = Counter()
        used = Counter()
        for wx in range((x0-1)//8, (x1+1)//8+1):
            for wy in range((y0-1)//8, (y1+1)//8+1):
                for x,y,z,_,stack in self.chunk(wx,wy):
                    if not (lower<=z<=upper and x0-1<=x<=x1+1 and y0-1<=y<=y1+1):
                        continue
                    walls = {}
                    floor = None
                    stairs = None
                    grime = []
                    attachments = []
                    for name in stack:
                        p = self.properties.get(name,{})
                        if name.startswith(('roofs_','rooftop_')) or '_roofs_' in name:
                            continue
                        if name in self.sources or name in self.attachments:
                            tiles.append([x,y,z,name])
                        if (x,y,z) in inside and 'solidfloor' in p:
                            floor = name
                        if name.startswith(('overlay_grime_','overlay_wall_','floors_burnt_','walls_burnt_')):
                            grime.append(name)
                        if name in self.attachments:
                            attachments.append(name)
                        for axis in ('N','W'):
                            neighbor = (x,y-1,z) if axis=='N' else (x-1,y,z)
                            if (x,y,z) not in inside and neighbor not in inside:
                                continue
                            wall = any(k+axis in p for k in ('Wall','DoorWall','Window','doorFr','cut')) or 'WallNW' in p
                            door = 'door'+axis in p and p.get('CustomName') not in ('Toilet','Curtain')
                            window = 'window'+axis in p and p.get('CustomName') not in ('Toilet','Curtain')
                            if not (wall or door or window):
                                continue
                            item = walls.setdefault(axis,dict(axis=axis,sources=[],wall=False,door=False,window=False,opening=''))
                            source_name = p.get('CornerNorthWall' if axis=='N' else 'CornerWestWall',name) if 'WallNW' in p else name
                            if source_name not in self.properties:
                                source_name = name
                            item['sources'].append(source_name)
                            item['wall'] |= wall
                            item['door'] |= door
                            item['window'] |= window
                            source = self.sources.get(source_name)
                            if wall and source:
                                item['boundary'] = source['type'] if source['archetype']=='boundary' else 'boundary.wall'
                                if source.get('opening'):
                                    item['openingConfig'] = source['opening']
                            if door or window:
                                item['insertSource'] = source_name
                                item['opening'] = 'door' if door else 'window'
                                if source:
                                    item['insert'] = source['type']
                                    item['state'] = source.get('state',source.get('nativeEvidence',{}).get('spriteState','closed'))
                                    item['mirrorU'] = source.get('nativeEvidence',{}).get('mirrorU',False)
                            material = self.book['tiles'].get(source_name)
                            if wall and material:
                                item['materialSource'] = material
                                if source_name.startswith('walls_exterior_'):
                                    exterior[material] += 1
                                else:
                                    for address in ((x,y,z),neighbor):
                                        if address in room_at:
                                            room_finishes[room_at[address]][material] += 1
                            used[source_name] += 1
                        if (x,y,z) in inside:
                            for axis in ('N','W'):
                                for i,part in enumerate(('B','M','T')):
                                    if 'stairs'+part+axis in p:
                                        stairs = dict(axis=axis,part=i,source=name)
                    if floor or walls or stairs or attachments or grime:
                        cells[(x,y,z)] = dict(address=[x,y,z],floor=floor,walls=list(walls.values()),stairs=stairs,
                                              attachmentSources=attachments,grime=grime)
        outside_material = exterior.most_common(1)[0][0] if exterior else None
        finishes = {room:counts.most_common(1)[0][0] for room,counts in room_finishes.items()}
        for address,cell in cells.items():
            x,y,z = address
            for wall in cell['walls']:
                a,b = ((x,y-1,z),(x,y,z)) if wall['axis']=='N' else ((x,y,z),(x-1,y,z))
                for key,side in [('surfaceA',a),('surfaceB',b)]:
                    wall[key] = finishes.get(room_at.get(side)) if side in room_at else outside_material
                    wall[key] = wall[key] or (wall.get('materialSource') if side not in room_at else None)
                wall.setdefault('boundary','boundary.wall')
        return dict(schema=2,id=building['id'],label=building.get('label',''),bounds=building['bounds'],
                    minLevel=lower,levels=upper+1,metric=resolve(),footprint=sorted(inside),
                    interior=sorted({(x,y,r['z']) for r in rooms if r['name']!='emptyoutside' for a,b,w,d in r['rects'] for x in range(a,a+w) for y in range(b,b+d)}),
                    cells=[cells[k] for k in sorted(cells)],nativeTiles=tiles,
                    roomFinishes=finishes,exteriorFinish=outside_material,usedSources=dict(used))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--building')
    parser.add_argument('--limit',type=int)
    parser.add_argument('--rooms-only',nargs='*')
    parser.add_argument('--definitions-only',action='store_true')
    args=parser.parse_args()
    if args.rooms_only is not None:
        entries=json.loads((OUTPUT/'resolved/inventory.json').read_text('utf-8'))['buildings']
        selected=set(args.rooms_only)
        if selected:
            entries=[entry for entry in entries if entry['id'] in selected]
            if {entry['id'] for entry in entries}!=selected:raise ValueError('Unknown targeted building IDs')
        headers={};updated=0
        for entry in entries:
            path=Path(entry['file']);facts=json.loads(gzip.decompress(path.read_bytes()))
            if 'interior' in facts:continue
            region='_'.join(entry['id'].split('_')[:2])
            if region not in headers:headers[region]=header(NATIVE/(region+'.lotheader'))
            native=next(building for building in headers[region]['buildings'] if building['id']==entry['id'])
            facts['interior']=sorted({(x,y,room['z']) for room in native['rooms'] if room['name']!='emptyoutside' for a,b,w,d in room['rects'] for x in range(a,a+w) for y in range(b,b+d)})
            publish(path,facts);updated+=1
        print(json.dumps(dict(prepared_room_footprints=updated,selected_buildings=len(entries))),flush=True)
        return
    OUTPUT.mkdir(parents=True,exist_ok=True)
    source=NativeConstructionSource()
    digest=hashlib.sha256(Path(__file__).read_bytes())
    for path in sorted((ROOT.parent/'laboratory/geometry').rglob('*')):
        if path.suffix in ('.py','.mjs','.json'):digest.update(path.read_bytes())
    digest.update((ROOT.parent/'materials-lab/materials.json').read_bytes())
    revision=digest.hexdigest()
    publish(OUTPUT/'definitions.json',source.definitions(revision))
    if args.definitions_only:
        print(json.dumps(dict(prepared_definitions=True,revision=revision)),flush=True)
        return
    entries=[]
    totals=Counter()
    for hp in sorted(NATIVE.glob('*.lotheader')):
        rx,ry=map(int,hp.stem.split('_'))
        h=source.region_header(rx,ry)
        for building in h['buildings']:
            if args.building and building['id']!=args.building:continue
            target=OUTPUT/'buildings'/(building['id']+'.json.gz')
            stamp=OUTPUT/'buildings'/(building['id']+'.source')
            source_revision=hashlib.sha256((revision+str(hp.stat().st_mtime_ns)+str((NATIVE/f'world_{rx}_{ry}.lotpack').stat().st_mtime_ns)).encode()).hexdigest()
            if target.exists() and stamp.exists() and stamp.read_text()==source_revision:
                summary=json.loads((target.with_suffix('.summary')).read_text('utf-8'))
            else:
                snapshot=source.snapshot(building)
                if not snapshot:continue
                publish(target,snapshot)
                summary=dict(id=building['id'],bounds=building['bounds'],file=str(target.resolve()),
                             cells=len(snapshot['cells']),walls=sum(len(c['walls']) for c in snapshot['cells']),
                             floors=sum(bool(c['floor']) for c in snapshot['cells']),levels=snapshot['levels'],revision=source_revision)
                publish(target.with_suffix('.summary'),summary)
                stamp.write_text(source_revision,encoding='utf-8')
            entries.append(summary)
            totals.update({k:summary[k] for k in ('cells','walls','floors')})
            if len(entries)%100==0:
                publish(OUTPUT/'inventory.json',dict(schema=1,revision=revision,complete=False,buildings=entries,totals=dict(totals)))
                print(json.dumps(dict(buildings=len(entries),**totals)),flush=True)
            if args.limit and len(entries)>=args.limit:break
        if args.limit and len(entries)>=args.limit:break
    publish(OUTPUT/'inventory.json',dict(schema=1,revision=revision,complete=True,buildings=entries,totals=dict(totals)))
    print(json.dumps(dict(complete=True,buildings=len(entries),**totals)),flush=True)


if __name__=='__main__':main()
