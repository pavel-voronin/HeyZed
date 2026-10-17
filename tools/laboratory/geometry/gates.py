"""Native gate families with shared metric defaults, never sprite-sized holes."""
from .profile import resolve

def design(name, props):
    if props.get('DoorSound') == 'WoodLogGate': return 'log'
    if name.startswith('fixtures_doors_fences_01_'):
        index = int(name.rsplit('_', 1)[1])
        if index < 4: return 'ornate-low'
        if index < 8: return 'wood-low'
        if index < 12: return 'wood-painted-low'
        if index < 16: return 'wood-tall'
        if index < 20: return 'chain-low'
        if index < 24: return 'ornate-tall'
        if index < 28: return 'bars-tall'
        if index < 32: return 'bars-low'
        if index < 48: return 'ornate-double'
        if index < 64: return 'wood-double'
        if index < 80: return 'chain-double'
        if index < 96: return 'bars-double'
        if index < 112: return 'wood-double'
        if index < 128: return 'farm'
        return 'chain-tall'
    return None

LABELS = {'ornate-low':'Low metal gate', 'wood-low':'Low wooden gate',
 'wood-painted-low':'Painted wooden gate', 'wood-tall':'Tall wooden gate',
 'chain-low':'Low chain-link gate', 'ornate-tall':'Tall metal gate',
 'bars-tall':'Tall bar gate', 'bars-low':'Low bar gate',
 'ornate-double':'Double metal gate', 'wood-double':'Double wooden gate',
 'chain-double':'Double chain-link gate', 'bars-double':'Double bar gate',
 'farm':'Farm gate', 'log':'Log gate', 'chain-tall':'Tall chain-link gate'}

def gate_profile(name, props, profile=None, section=False):
    identity = design(name, props)
    if identity is None: return None
    p = profile or resolve()
    double = 'DoubleDoor' in props
    low = identity.endswith('-low') or identity in ('wood-painted-low','farm','log')
    height = min(p['lowWallHeight'] if low else p['doorTop'], p['floorHeight']-p['slabDepth'])
    cells = 1 if section or not double else 4
    width = cells*p['tileSize']
    points = [[0,0],[0,height],[width,height],[width,0]]
    return dict(id='gate-'+identity,label=LABELS[identity],shape='rectangle',status='Approved',
        bounds=[0,width,0,height],points=points,joins=[],
        construction=dict(mechanism='hinged-gate',gateDesign=identity,double=double,
            sectionRole=int(props['DoubleDoor']) if section and double else None,
            hinge='left',openAngleDegrees=90),cells=cells)
