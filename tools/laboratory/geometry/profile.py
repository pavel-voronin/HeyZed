"""Canonical architectural metrics. Grid addresses are never editable."""
import json
import math

MODEL_ID = 'heyzed-geometry/1'
MODEL_SOURCE = 'tools/laboratory/geometry/profile.py'
TILE_SIZE = 1
RASTER_WIDTH = 64
RASTER_HEIGHT = 192
# Legacy sprite skins are separate from the exact structural boundary.
LEGACY_WALL_SKIN_CLEARANCE = .001
LEGACY_MOUNTED_SKIN_CLEARANCE = .008
FLOOR_HEIGHT = 2.4494900703430176
# Defaults are a baseline, never overwritten by a browser's preview.
FIELDS = {
    'wallThickness': ('Wall thickness', .12, .05, .4),
    'slabDepth': ('Slab depth', .15, .04, .4),
    'roofDepth': ('Roof shell depth', .04, .01, .12),
    'doorLeft': ('Door left', .1, .02, .35),
    'doorRight': ('Door right', .9, .65, .98),
    'doorTop': ('Door height', 2.05, 1.6, 2.3),
    'windowLeft': ('Window left', .1, .02, .35),
    'windowRight': ('Window right', .9, .65, .98),
    'windowBottom': ('Window sill', .85, .3, 1.3),
    'windowTop': ('Window top', 2.05, 1.5, 2.3),
    'windowFrameWidth': ('Frame width', .027, .01, .1),
    'windowFrameOverlap': ('Frame overlap', .005, 0, .015),
    'windowFrameProjection': ('Frame projection', .017, .005, .08),
    'windowSashDepth': ('Sash depth', .04, .015, .08),
    'windowSashGap': ('Sash track gap', .008, 0, .025),
    'windowPaneThickness': ('Window glass thickness', .004, .002, .01),
    'doorThickness': ('Door thickness', .045, .02, .12),
    'doorLeafClearance': ('Door leaf clearance', .003, .001, .01),
    'doorHingeRadius': ('Door hinge radius', .006, .003, .012),
    'doorStileWidth': ('Door stile width', .065, .04, .12),
    'doorRailWidth': ('Door rail width', .075, .04, .15),
    'doorBarWidth': ('Door bar width', .018, .01, .04),
    'doorPanelRelief': ('Door panel relief', .006, .001, .009),
    'doorHandleLength': ('Door handle length', .11, .08, .16),
    'doorHandleProjection': ('Door handle projection', .05, .03, .08),
    'doorHandleDiameter': ('Door handle diameter', .016, .01, .025),
    'doorHandleBackset': ('Door handle backset', .06, .04, .09),
    'doorHandlePlateWidth': ('Handle backplate width', .036, .02, .06),
    'doorHandlePlateHeight': ('Handle backplate height', .18, .10, .25),
    'doorHandlePlateDepth': ('Handle backplate depth', .004, .002, .008),
    'doorHandleHeight': ('Door handle height', .95, .8, 1.1),
    'glassThickness': ('Glass thickness', .04, .002, .06),
    'lowWallHeight': ('Low wall height', 1.15, .5, 1.5),
    'fasciaDepth': ('Fascia depth', .04, .02, .08),
    'fasciaBandHeight': ('Fascia band height', 35*FLOOR_HEIGHT/192, .25, .7),
    'fasciaDarkBandHeight': ('Fascia lower band', .11, .05, .2),
    'fasciaClearance': ('Fascia mounting clearance', .005, 0, .02),
    'eaveTrimDepth': ('Eave trim depth', .008, .003, .025),
    'eaveTrimHeight': ('Eave trim height', 8*FLOOR_HEIGHT/192, .04, .2),
    'overhangWidth': ('Overhang width', .4, .04, .75),
    'railingWidth': ('Rail and post width', .035, .015, .08),
    'decorDepth': ('Mounted detail depth', .02, .005, .06),
    'signDepth': ('Sign body depth', .04, .01, .08),
    'mountProjection': ('Mount projection', .07, .02, .15),
    'mountWidth': ('Mount width', .03, .01, .06),
    'stairSteps': ('Steps per tile', 5, 3, 8),
}
DEFAULT = {key: value[1] for key, value in FIELDS.items()}


def resolve(values=None):
    values = {} if values is None else values
    if not isinstance(values, dict) or set(values)-FIELDS.keys():
        raise ValueError('Unknown geometry profile field')
    p = dict(DEFAULT)
    for key, value in values.items():
        if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value):
            raise ValueError('Invalid profile value: '+key)
        if not FIELDS[key][2] <= value <= FIELDS[key][3]:
            raise ValueError('Profile value out of range: '+key)
        if key == 'stairSteps' and value != int(value):
            raise ValueError('Steps per tile must be an integer')
        p[key] = value
    if p['doorLeft'] >= p['doorRight'] or p['windowLeft'] >= p['windowRight'] or p['windowBottom'] >= p['windowTop']:
        raise ValueError('Opening bounds must be ordered')
    if p['doorTop']+p['windowFrameOverlap'] > FLOOR_HEIGHT-p['slabDepth'] or p['windowTop']+p['windowFrameOverlap'] > FLOOR_HEIGHT-p['slabDepth']:
        raise ValueError('Opening top must fit below the slab')
    if p['windowFrameOverlap'] >= p['windowFrameWidth']:
        raise ValueError('Frame overlap must be smaller than frame width')
    if p['fasciaDarkBandHeight']>=p['fasciaBandHeight']:
        raise ValueError('Lower fascia band must fit inside the fascia')
    p.update(tileSize=TILE_SIZE, floorHeight=FLOOR_HEIGHT, wallHalfExtent=p['wallThickness']/2,
             wallSkinOffset=p['wallThickness']/2, mountedSkinOffset=p['wallThickness']/2,
             windowFrameHalfDepth=p['wallThickness']/2+p['windowFrameProjection'],
             windowFrameDepth=p['wallThickness']+2*p['windowFrameProjection'],
             doorLeafCenter=0,
             doorRevealDepth=(p['wallThickness']-p['doorThickness'])/2,
             doorLatchStileWidth=max(p['doorStileWidth'],p['doorHandleBackset']+p['doorHandlePlateWidth']/2+p['doorLeafClearance']),
             doorHingeDepth=p['doorThickness']/2+p['doorHingeRadius'],
             windowSashHalfDepth=p['windowSashDepth']/2,
             windowSashTrackOffset=(p['windowSashDepth']+p['windowSashGap'])/2,
             id=MODEL_ID, slabProvider='roof-lab.profile-boundary', roofProvider='preview-profile')
    return p


def key(values=None):
    p = resolve(values)
    return json.dumps({k: p[k] for k in FIELDS}, sort_keys=True, separators=(',', ':'))


def descriptor():
    from .openings import descriptor as opening_descriptor
    from .door_styles import descriptor as door_style_descriptor
    return dict(id=MODEL_ID, source=MODEL_SOURCE, openings=opening_descriptor(), doorStyles=door_style_descriptor(), defaults=dict(DEFAULT), profile=resolve(),
                metric=dict(tileSize=TILE_SIZE, floorHeight=FLOOR_HEIGHT, rasterWidth=RASTER_WIDTH, rasterHeight=RASTER_HEIGHT),
                fields=[dict(key=k, label=v[0], default=v[1], min=v[2], max=v[3],
                             step=1 if k=='stairSteps' else .001) for k,v in FIELDS.items()])
