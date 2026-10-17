"""Shared construction addresses and laboratory preparation, independent of maps."""
import json
from functools import lru_cache
from pathlib import Path
from .profile import resolve
from .openings import section_geometry, section_for_tile
from .apertures import profiles, profile_for_tile, contour, sample
from .door_styles import classify, descriptor as door_styles
from .gates import design as gate_design, gate_profile

SOURCE = Path(__file__).with_name('construction_families.json')

@lru_cache(maxsize=1)
def families():
    return json.loads(SOURCE.read_text('utf-8'))['families']

def family_for_tile(name):
    return next((f for f in families() if name in f['wallTiles']),None)

def family_geometry(family,profile=None):
    if family['kind']=='sliding-door':
        p=profile or resolve();width=family['cells']*p['tileSize'];top=p['floorHeight']-p['slabDepth']
        points=[[0,0],[0,top],[width,top],[width,0]]
        return dict(id=family['id'],label=family['label'],status='Approved',shape='rectangle',
            bounds=[0,width,0,top],points=points,joins=[],construction=dict(mechanism='sliding-door'))
    if family.get('profileId'):return section_geometry(family['profileId'])
    parameters=dict(family['parameters']);placement=None
    if family.get('verticalPlacement')=='below-ceiling-slab':
        p=profile or resolve();source_top=parameters['top']*p['floorHeight']
        parameters['top']-=p['slabDepth']/p['floorHeight']
        placement=dict(verticalScale=parameters['top']*p['floorHeight']/source_top,verticalOffset=0)
    path=contour('rectangle',parameters);points=sample(path)
    door=section_geometry('opening.door.01')
    return dict(id=family['id'],status='Approved',shape='rectangle',contour=path,points=points,
        bounds=[min(q[0] for q in points),max(q[0] for q in points),min(q[1] for q in points),max(q[1] for q in points)],
        joins=[],construction=door['construction'],placement=placement)

def partial_door_geometry(profile=None):
    p=profile or resolve();leaf=door_styles()['partialHeightLeaf']
    aperture=section_geometry('opening.door.01')
    return dict(aperture,construction=dict(aperture['construction'],leafBottom=leaf['bottom']*p['floorHeight'],leafTop=leaf['top']*p['floorHeight']))

@lru_cache(maxsize=1)
def window_profiles():return {p['id']:p for p in profiles()}

def tile_profile(name,props,side):
    if gate_design(name,props) is not None:return None
    # An incomplete patio pair must never fall back to a hinged door/window.
    if props.get('DoorSound')=='SlidingGlassDoor':return None
    match=profile_for_tile(name,props,side)
    if match:return match['profile']
    entry=section_for_tile(name)
    if entry and entry['kind']=='door':return section_geometry(entry['id'])
    if any(k in props for k in ('windowN','windowW','WindowN','WindowW')):
        shape=props.get('WindowShape')
        if not shape and not name.startswith('fixtures_windows_'):return None
        identity='window-'+str(shape)+'-0' if shape else 'window-11-0' if props.get('GrimeType')=='FullWindow' else 'window-9-1' if name.startswith('fixtures_windows_01_') else 'window-9-0'
        return window_profiles().get(identity,window_profiles()['window-9-0'])
    if any(k in props for k in ('doorN','doorW','DoorWallN','DoorWallW','doorFrN','doorFrW')):return section_geometry('opening.door.01')
    return None

def prepare(tiles,properties,interior_cells=None,profile=None):
    """Return whole constructions once; native cell orientation places shared models."""
    p=profile or resolve();by_cell={};state_links={}
    for name,props in properties.items():
        sheet,_,index=name.rpartition('_')
        if not index.isdigit():continue
        for key,state in [('OpenTileOffset','open'),('SmashedTileOffset','broken'),('GlassRemovedOffset','glassRemoved')]:
            if key in props:state_links[sheet+'_'+str(int(index)+int(props[key]))]=(name,state)
    for i,tile in enumerate(tiles):by_cell.setdefault(tuple(tile[:3]),[]).append(i)
    consumed=set();requests=[]
    def oriented(props):
        return next((s for s in ('N','W') if any(k+s in props for k in ('window','door','Window','Wall','DoorWall','doorFr','cut'))),None)
    def insert(i,side):
        props=properties.get(tiles[i][3],{})
        return ('window'+side in props or 'door'+side in props) and props.get('CustomName') not in ('Toilet','Curtain')
    def add(indices,side,aperture,kind,wall=True,cells=1):
        indices=sorted(set(indices));anchor=min((tiles[i][:3] for i in indices),key=lambda q:tuple(q))
        candidates=[i for i in indices if insert(i,side)]
        chosen=candidates[0] if candidates else None
        name=tiles[chosen][3] if chosen is not None else ''
        base,state=state_links.get(name,(name,'closed'));props=properties.get(base,properties.get(name,{}))
        if kind=='garage':state='open' if any(int(properties.get(tiles[i][3],{}).get('GarageDoor',1))>3 for i in candidates) else 'closed'
        reference=dict(name=base,properties=props,doorAppearance=classify(dict(name=base,properties=props)))
        requests.append(dict(anchor=anchor,side=side,aperture=aperture,kind=kind,state=state,wall=wall,
            insert=bool(candidates),reference=reference,cells=cells,sources=[tiles[i][3] for i in indices]))
        if kind in ('garage','sliding-door'):
            axis=1 if side=='N' else 0
            scores={sign:0 for sign in (-1,1)}
            for i in indices:
                cell=list(tiles[i][:3])
                for sign in (-1,1):
                    neighbor=cell.copy();neighbor[axis]+=0 if sign>0 else -1
                    if interior_cells is not None:scores[sign]+=tuple(neighbor) in interior_cells
                    else:scores[sign]+=any('solidfloor' in properties.get(tiles[j][3],{}) for j in by_cell.get(tuple(neighbor),[]))
            requests[-1]['interiorSign']=-1 if scores[-1]>scores[1] else 1
            # The frame consumes native door cells; adjacent wall finishes remain authoritative.
            along=0 if side=='N' else 1
            neighbors=[]
            for step in (-1,cells):
                cell=list(anchor);cell[along]+=step
                neighbors.extend(tiles[j][3] for j in by_cell.get(tuple(cell),[]) if 'Wall'+side in properties.get(tiles[j][3],{}) and 'GarageDoor' not in properties.get(tiles[j][3],{}))
            if neighbors:requests[-1]['wallSource']=neighbors[0]
        consumed.update(indices)
    # The locked full-height window and adjacent sliding leaf are one native
    # patio entrance. Both actual cells are required, on the same floor/edge.
    for family in (f for f in families() if f['kind']=='sliding-door'):
        for assembly in family['assemblies']:
            side=assembly['side'];axis=0 if side=='N' else 1
            for i,tile in enumerate(tiles):
                if i in consumed or tile[3] not in (assembly['closed'],assembly['open']) or tile[3] not in properties:continue
                peers=[]
                for direction in (-1,1):
                    cell=list(tile[:3]);cell[axis]+=direction
                    peers.extend(j for j in by_cell.get(tuple(cell),[]) if j not in consumed and
                        tiles[j][3] in (assembly['fixed'],assembly['empty']) and tiles[j][3] in properties)
                if len(peers)!=1:continue
                j=peers[0]
                add([i,j],side,family_geometry(family,p),'sliding-door',wall=False,cells=2)
                requests[-1].update(state='open' if tile[3]==assembly['open'] else 'closed',
                    fixedState='glassRemoved' if tiles[j][3]==assembly['empty'] else 'closed',
                    mirrorU=tile[axis]>tiles[j][axis],insert=True)
    # IsoDoor native DoubleDoor offsets: four sections, two actual leaves.
    for i,tile in enumerate(tiles):
        if i in consumed or tile[3] not in properties:continue
        name=tile[3];props=properties[name]
        if gate_design(name,props) is None:continue
        side=oriented(props);role=int(props.get('DoubleDoor',0));opened=role>4
        if role:
            if not side:continue
            index=(role-1)%4
            offsets=([(0,0),(0,1),(3,1),(3,0)] if opened else [(0,0),(1,0),(2,0),(3,0)]) if side=='N' else ([(0,0),(1,0),(1,-3),(0,-3)] if opened else [(0,0),(0,-1),(0,-2),(0,-3)])
            root=(tile[0]-offsets[index][0],tile[1]-offsets[index][1],tile[2])
            sheet,_,number=name.rpartition('_');block=int(number)//16;own=[]
            for part,(dx,dy) in enumerate(offsets):
                peer=next((j for j in by_cell.get((root[0]+dx,root[1]+dy,root[2]),[]) if j not in consumed and tiles[j][3] in properties and
                    tiles[j][3].rsplit('_',1)[0]==sheet and int(tiles[j][3].rsplit('_',1)[1])//16==block and
                    oriented(properties[tiles[j][3]])==side and int(properties[tiles[j][3]].get('DoubleDoor',0))==part+1+(4 if opened else 0)),None)
                if peer is None:break
                own.append(peer)
            if len(own)!=4:continue
            add(own,side,gate_profile(name,props,p),'gate',wall=False,cells=4)
            requests[-1].update(anchor=[root[0],root[1]-(3 if side=='W' else 0),root[2]],state='open' if opened else 'closed',insert=True)
        else:
            base=name
            if not any(k in props for k in ('doorN','doorW')):
                sheet,_,number=name.rpartition('_');base=sheet+'_'+str(int(number)-2)
                closed=properties.get(base,{})
                if gate_design(base,closed)!=gate_design(name,props) or not any(k in closed for k in ('doorN','doorW')):continue
                props=closed;side=oriented(props);opened=True
            if not side:continue
            add([i],side,gate_profile(base,props,p),'gate',wall=False)
            requests[-1].update(state='open' if opened else 'closed',insert=True)
    # Short leaves retain their native vertical placement inside a full door frame.
    for i,tile in enumerate(tiles):
        if i in consumed or tile[3] not in properties:continue
        name=tile[3];props=properties[name];side=oriented(props)
        sheet,_,number=name.rpartition('_')
        if not side or not number.isdigit() or not insert(i,side):continue
        if not (sheet=='fixtures_doors_02' and 16<=int(number)<=27):continue
        aperture=partial_door_geometry(p)
        add([i],side,aperture,'standalone-door',wall=False)
    # Native GarageDoor establishes a contiguous left/middle/right chain.
    for i,tile in enumerate(tiles):
        props=properties.get(tile[3],{});side=oriented(props)
        if i in consumed or not side or 'GarageDoor' not in props or (int(props['GarageDoor'])-1)%3!=0:continue
        axis=0 if side=='N' else 1
        for direction in (1,-1):
            chain=[i];cell=list(tile[:3]);complete=False
            for _ in range(1,64):
                cell[axis]+=direction
                peers=[j for j in by_cell.get(tuple(cell),[]) if 'GarageDoor' in properties.get(tiles[j][3],{}) and oriented(properties[tiles[j][3]])==side and j not in consumed]
                if not peers:break
                j=peers[0];role=(int(properties[tiles[j][3]]['GarageDoor'])-1)%3
                if role==0:break
                chain.append(j)
                if role==2:complete=True;break
            if complete:add(chain,side,None,'garage',cells=len(chain));break
    for i,tile in enumerate(tiles):
        if i in consumed:continue
        name=tile[3];props=properties.get(name,{});side=oriented(props)
        if not side or props.get('CustomName') in ('Toilet','Curtain') or 'CornerNorthWall' in props:continue
        family=family_for_tile(name)
        if 'doorFr'+side in props and any(j!=i and j not in consumed and any(k+side in properties.get(tiles[j][3],{}) for k in ('Wall','DoorWall','cut')) for j in by_cell[tuple(tile[:3])]):continue
        wall=any(k+side in props for k in ('Window','DoorWall','doorFr','cut')) or ('Wall'+side in props and any(insert(j,side) for j in by_cell[tuple(tile[:3])]))
        if not family and not wall:continue
        if family and family['kind'] in ('garage','sliding-door'):continue
        own=[i]+[j for j in by_cell[tuple(tile[:3])] if j not in consumed and (insert(j,side) or 'doorFr'+side in properties.get(tiles[j][3],{}))]
        if not family and 'doorFr'+side in props:wall=False
        cells=family.get('cells',1) if family else 1
        if cells==2:
            axis=0 if side=='N' else 1
            companion=name.rsplit('_',1)[0]+'_'+str(int(name.rsplit('_',1)[1])^1)
            found=None
            for direction in (-1,1):
                cell=list(tile[:3]);cell[axis]+=direction
                found=next((j for j in by_cell.get(tuple(cell),[]) if tiles[j][3]==companion and j not in consumed),None)
                if found is not None:break
            # Standalone catalogue specimens do not invent another occupied cell.
            if found is None:continue
            own.extend([found]+[j for j in by_cell[tuple(tiles[found][:3])] if j not in consumed and insert(j,side)])
        aperture=family_geometry(family) if family else tile_profile(name,props,side)
        if aperture is None:
            for j in own:
                base,_=state_links.get(tiles[j][3],(tiles[j][3],'closed'))
                aperture=tile_profile(base,properties.get(base,{}),side)
                if aperture is not None:break
        if aperture is None:continue
        add(own,side,aperture,family['kind'] if family else 'window' if aperture['id'].startswith('window-') or 'Window'+side in props or any('window'+side in properties.get(tiles[j][3],{}) for j in own) else 'door',cells=cells)
    for i,tile in enumerate(tiles):
        props=properties.get(tile[3],{});side=oriented(props)
        if i in consumed or not side or not insert(i,side) or 'GarageDoor' in props:continue
        base,_=state_links.get(tile[3],(tile[3],'closed'))
        aperture=tile_profile(base,properties.get(base,props),side)
        if aperture is None:continue
        kind='window' if 'window'+side in props else 'door'
        add([i],side,aperture,kind,wall=False)
    from .attachments import bind
    bind(tiles,requests,consumed,properties)
    return dict(requests=requests,consumed=consumed)
