"""Shared wall cuts and baseline inserts. Z is up; N/W are native tile edges."""
import json
from copy import deepcopy
from functools import lru_cache
from pathlib import Path
from .profile import resolve, MODEL_ID, MODEL_SOURCE

def wall_boxes(axis, opening, p, low=False, ends=(0,1)):
    # The next floor datum is the slab's top, not the wall's top. Reserve its
    # full depth even at exposed edges and where the neighboring tile is void.
    h=p['lowWallHeight'] if low else p['floorHeight']-p['slabDepth'];t=p['wallThickness']/2
    start,end=ends;rects=[(start,end,0,h)]
    if opening:
        prefix='window' if opening==2 else 'door'
        left,right,top=(p[prefix+k] for k in ('Left','Right','Top'))
        bottom=p['windowBottom'] if opening==2 else 0
        rects=[(start,left,0,h),(right,end,0,h),(left,right,top,h)]
        if bottom:rects.append((left,right,0,bottom))
    return [([a,-t,c,b,t,d] if axis=='north' else [-t,a,c,t,b,d]) for a,b,c,d in rects if d>c]


def insert_boxes(axis, window, p):
    prefix='window' if window else 'door'
    left,right,top=(p[prefix+k] for k in ('Left','Right','Top'))
    bottom=p['windowBottom'] if window else 0
    w=p['windowFrameWidth'];overlap=p['windowFrameOverlap']
    depth=p['windowFrameHalfDepth'] if window else p['wallThickness']/2
    a,b,c,d=left-overlap,right+overlap,bottom-overlap if window else 0,top+overlap
    rects=[(a,a+w,c,d,-depth,depth),(b-w,b,c,d,-depth,depth),(a+w,b-w,d-w,d,-depth,depth)]
    if window:rects.append((a+w,b-w,c,c+w,-depth,depth))
    pane=p['glassThickness']/2 if window else p['doorThickness']/2
    if window:
        middle=(c+d)/2
        rects.append((a+w,b-w,middle-w/2,middle+w/2,-depth,depth))
        rects.extend([(a+w,b-w,c+w,middle-w/2,-pane,pane),
                      (a+w,b-w,middle+w/2,d-w,-pane,pane)])
    else:rects.append((a+w,b-w,c,d-w,-pane,pane))
    return [([u,n0,z0,v,n1,z1] if axis=='north' else [n0,u,z0,n1,v,z1]) for u,v,z0,z1,n0,n1 in rects]



# Existing laboratory baselines, not the final approved vanilla shape registry.
BASELINES = {
    'window.standard': ('Standard window baseline', 'window'),
    'door.standard': ('Standard door baseline', 'door'),
    'window.full-cell': ('Full-cell glazing baseline', 'window'),
}


def opening_profile(identity, profile=None):
    if identity not in BASELINES:
        raise ValueError('Unknown opening profile: '+identity)
    p=dict(profile) if profile is not None else resolve()
    if identity=='window.full-cell':
        overlap=p['windowFrameOverlap']
        p.update(windowLeft=overlap,windowRight=p['tileSize']-overlap,
                 windowBottom=overlap,windowTop=p['floorHeight']-p['slabDepth']-overlap)
    return p


def card(identity, profile=None):
    p=opening_profile(identity,profile);label,kind=BASELINES[identity]
    left,right,top=(p[kind+k] for k in ('Left','Right','Top'))
    bottom=p['windowBottom'] if kind=='window' else 0
    return dict(id=identity,label=label,status='baseline-awaiting-review',kind=kind,
                model=MODEL_ID,source=MODEL_SOURCE,bounds=[left,right,bottom,top],
                size=[right-left,top-bottom],depth=p['wallThickness'],
                placement=dict(origin='native tile edge at floor datum',axes='X/Y horizontal, Z up',
                               orientations=dict(N='along X, depth Y',W='along Y, depth X')),
                joint=dict(frameWidth=p['windowFrameWidth'],frameOverlap=p['windowFrameOverlap'],
                           depthCentered=True,sectionJoin='Unresolved until composite profiles are approved'))


@lru_cache(maxsize=1)
def _sections():
    return json.loads(Path(__file__).with_name('opening_sections.json').read_text(encoding='utf-8'))['sections']


def sections():
    """Reviewed native membership; pending sections have no buildable geometry."""
    return deepcopy(_sections())


def section(identity):
    for entry in _sections():
        if entry['id']==identity:return deepcopy(entry)
    raise ValueError('Unknown opening section: '+identity)


def section_geometry(identity):
    """Resolve the approved section in the canonical metric, independent of rasters."""
    from .apertures import contour, sample
    entry=section(identity)
    if entry['geometryStatus']!='approved' or not entry['geometry']:
        raise ValueError('Section geometry is not approved: '+identity)
    geometry=entry['geometry'];path=contour(geometry['shape'],geometry['parameters']);points=sample(path)
    return dict(id=identity,label=entry['label'],status='Approved',shape=geometry['shape'],
                bounds=[min(p[0] for p in points),max(p[0] for p in points),min(p[1] for p in points),max(p[1] for p in points)],
                points=points,contour=path,construction=entry.get('construction'),
                joins=[edge for edge,value in [('left',geometry['parameters']['left']),('right',geometry['parameters']['right'])]
                       if value==(0 if edge=='left' else 1)],
                source='tools/laboratory/geometry/opening_sections.json')


def section_for_tile(name):
    for entry in _sections():
        if any(name in names for names in entry['bindings'].values()):return deepcopy(entry)
    return None


def descriptor(profile=None):
    from .apertures import profiles, SOURCE
    approved=profiles()
    import json
    from pathlib import Path
    section_layouts=json.loads(Path(__file__).with_name('window-section-layouts.json').read_text('utf-8'))
    return dict(model=MODEL_ID,source=MODEL_SOURCE,windowSections=section_layouts,profiles=approved,profileSource=SOURCE,cards=[card(k,profile) for k in BASELINES],
                sections=sections(),sectionSource='tools/laboratory/geometry/opening_sections.json',
                nativeShapeBindings={str(n):[p['id'] for p in approved if p['nativeShape']==str(n)] for n in range(1,20)},bindingStatus='Approved aperture profiles; baseline inserts remain unassigned')


def preview(identity,side='N',values=None):
    if side not in ('N','W'):raise ValueError('Opening side must be N or W')
    p=opening_profile(identity,resolve(values));window=BASELINES[identity][1]=='window'
    axis='north' if side=='N' else 'west'
    inserts=insert_boxes(axis,window,p)
    frame_count=len(inserts)-(2 if window else 1)
    return dict(card=card(identity,p),profile=p,side=side,
                wall=wall_boxes(axis,2 if window else 1,p),
                frame=inserts[:frame_count],glass=inserts[frame_count:] if window else [],
                panel=[] if window else inserts[frame_count:])
