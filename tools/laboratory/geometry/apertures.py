pass
import json
from math import tan, pi
from pathlib import Path
from .profile import TILE_SIZE, FLOOR_HEIGHT, resolve

SOURCE = 'tools/laboratory/geometry/aperture_profiles.json'


def contour(shape, parameters):
    l, r = (parameters[k] * TILE_SIZE for k in ('left', 'right'))
    b, t = (parameters[k] * FLOOR_HEIGHT for k in ('bottom', 'top'))
    s = parameters.get('spring', parameters['bottom']) * FLOOR_HEIGHT
    k = 4 * tan(pi / 8) / 3
    segments = []
    def line(u, z): segments.append(dict(kind='line', to=[u, z]))
    def cubic(a, c, end): segments.append(dict(kind='cubic', c1=a, c2=c, to=end))
    mid = (l+r)/2
    if shape == 'ellipse':
        cy=(b+t)/2; rx=(r-l)/2; rz=(t-b)/2; start=[l,cy]
        cubic([l,cy+k*rz],[mid-k*rx,t],[mid,t])
        cubic([mid+k*rx,t],[r,cy+k*rz],[r,cy])
        cubic([r,cy-k*rz],[mid+k*rx,b],[mid,b])
        cubic([mid-k*rx,b],[l,cy-k*rz],start)
    else:
        start=[l,b];line(l,t if shape=='rectangle' or shape=='quarter-ellipse' and parameters['crest']=='left' else s)
        if shape=='rectangle': line(r,t)
        elif shape=='quarter-ellipse':
            if parameters['crest']=='left': cubic([l+k*(r-l),t],[r,s+k*(t-s)],[r,s])
            else: cubic([l,s+k*(t-s)],[r-k*(r-l),t],[r,t])
        elif shape=='elliptic-arch':
            rx=(r-l)/2;rz=t-s
            cubic([l,s+k*rz],[mid-k*rx,t],[mid,t])
            cubic([mid+k*rx,t],[r,s+k*rz],[r,s])
        elif shape=='pointed-arch':
                                                                                   
            rise=t-s;half=(r-l)/2
            cubic([l,s+rise/2],[mid-half/3,t-rise/6],[mid,t])
            cubic([mid+half/3,t-rise/6],[r,s+rise/2],[r,s])
        else: raise ValueError('Unknown aperture shape: '+shape)
        line(r,b);line(*start)
    return dict(start=start, segments=segments)


def sample(path, steps=16):
    result=[path['start']];a=path['start']
    for segment in path['segments']:
        end=segment['to']
        if segment['kind']=='line': result.append(end)
        else:
            c,d=segment['c1'],segment['c2']
            for i in range(1,steps+1):
                v=i/steps;u=1-v
                result.append([u**3*a[j]+3*u*u*v*c[j]+3*u*v*v*d[j]+v**3*end[j] for j in (0,1)])
        a=end
    return result[:-1]


def profiles():
    catalog=json.loads(Path(__file__).with_name('aperture_profiles.json').read_text(encoding='utf-8'))
    entries=catalog['profiles'];metrics=resolve();thickness=metrics['wallThickness']
    for entry in entries:
        entry['source']=SOURCE
        entry['placement']={'axes':'local u along wall, z up; depth centered on wall','depth':thickness}
        entry['contour']=contour(entry['shape'],entry['parameters']);entry['points']=sample(entry['contour'])
        for binding in entry['bindings'].values():
            turn=binding['quarterTurns']
            binding['origin']=[TILE_SIZE if turn==2 else 0,TILE_SIZE if turn==3 else 0,0]
        entry['joins']=[]
        for edge,axis,value in [('left',0,0),('right',0,TILE_SIZE),('bottom',1,0),('top',1,FLOOR_HEIGHT)]:
            on_edge=[p[1-axis] for p in entry['points'] if abs(p[axis]-value)<1e-9]
            if on_edge and max(on_edge)-min(on_edge)>1e-9:entry['joins'].append(edge)
        placement=entry.get('verticalPlacement',catalog.get('verticalPlacement','storey'))
        if placement not in ('storey','below-ceiling-slab'):raise ValueError('Unknown vertical placement: '+placement)
        offset=-metrics['slabDepth'] if placement=='below-ceiling-slab' else 0
        entry['verticalPlacement']=placement
        scale=1
        if entry.get('bottomPlacement')=='floor':
            bottom=entry['parameters']['bottom']*FLOOR_HEIGHT;top=entry['parameters']['top']*FLOOR_HEIGHT
            scale=(top+offset)/(top-bottom)
            if scale<=0:raise ValueError('Window does not fit above floor')
            offset=-bottom*scale
        entry['placement'].update(verticalOffset=offset,verticalScale=scale)
        entry['joinPlanes']={'left':0,'right':TILE_SIZE,'bottom':offset,'top':FLOOR_HEIGHT*scale+offset}
        for key in ('bottom','top','spring'):
            if key in entry['parameters']:entry['parameters'][key]=entry['parameters'][key]*scale+offset/FLOOR_HEIGHT
        entry['contour']=contour(entry['shape'],entry['parameters']);entry['points']=sample(entry['contour'])
    return entries


def profile(identity):
    for entry in profiles():
        if entry['id']==identity: return entry
    raise ValueError('Unknown aperture profile: '+identity)


def binding_side(properties):
    sides=[s for s in ('N','W') if any(k+s in properties for k in ('Window','cut','DoorWall'))]
    return sides[0] if len(sides)==1 else None


def matches(entry, name, properties):
    selector=entry.get('selector')
    if selector:
        return properties.get('WindowShape')==selector['value'] and any(k in properties for k in ('wall','cutN','cutW','DoorWallN','DoorWallW'))
    return any(name==g['tileset']+'_'+str(i) for v in entry['bindings'].values() for g in v.get('tileGroups',[]) for i in g['indices'])


def profile_for_tile(name, properties=None, side=None):
    properties=properties or {}
    for entry in profiles():
        if entry.get('selector') and matches(entry,name,properties):
            direction=side or binding_side(properties)
            return dict(profile=entry,side=direction,quarterTurns=entry['bindings'][direction]['quarterTurns'] if direction else None)
        for variant_side,variant in entry['bindings'].items():
            if any(name==g['tileset']+'_'+str(i) for g in variant.get('tileGroups',[]) for i in g['indices']): return dict(profile=entry,side=variant_side,quarterTurns=variant['quarterTurns'])
    return None
