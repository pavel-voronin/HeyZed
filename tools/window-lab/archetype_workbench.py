"""Native evidence adapter for laboratory archetypes; never infers shape from pixels."""
import json
from pathlib import Path
import sys
from functools import lru_cache

ROOT=Path(__file__).resolve().parent
GEOMETRY=ROOT.parent/'laboratory/geometry'
ASSIGNMENTS=ROOT/'.local/archetype-assignments.json'
sys.path.append(str(ROOT.parent/'laboratory'))


def semantic(properties,native_kind=None):
    if native_kind in ('IsoDoor','door'):return 'door'
    if native_kind in ('IsoWindow','window'):return 'window'
    if properties.get('CustomName') in ('Toilet','Curtain') or properties.get('MoveType')=='WindowObject':return None
    if any(k in properties for k in ('doorN','doorW','GarageDoor')):
        return 'door'
    if any(k in properties for k in ('windowN','windowW')):
        return 'window'
    if any(k in properties for k in ('wall','WallN','WallW','WallNW','WallNWTrans','WallSE','WindowN','WindowW','cutN','cutW','collideN','collideW','DoorWallN','DoorWallW','doorFrN','doorFrW','HoppableN','HoppableW','TallHoppableN','TallHoppableW','CornerNorthWall','CornerWestWall','FenceTypeLow','FenceTypeHigh')):
        return 'boundary'
    return None


def type_archetype(registry,identity,path=()):
    if isinstance(identity,str) and identity.startswith('archetype.'):
        result=identity.removeprefix('archetype.')
        if result in registry['archetypes']:return result
    if identity in path:raise ValueError('Cyclic type inheritance')
    entry=next((t for t in registry['types'] if t['id']==identity),None)
    if entry is None:raise ValueError('Unknown type: '+identity)
    inherited=type_archetype(registry,entry['extends'],(*path,identity)) if entry.get('extends') and any(t['id']==entry['extends'] for t in registry['types']) else None
    result=entry.get('archetype',inherited)
    if result not in registry['archetypes'] or inherited and inherited!=result:raise ValueError('Invalid archetype inheritance')
    return result


def source_opening(properties,archetype):
    if archetype in ('door','window'):return dict(archetype=archetype)
    if any(k in properties for k in ('DoorWallN','DoorWallW','doorFrN','doorFrW')):return dict(archetype='door')
    if any(k in properties for k in ('WindowN','WindowW','cutN','cutW')):return dict(archetype='window')
    return None


def resolve_source(tile,registry,bindings,material_book=None):
    archetype=semantic(tile.get('properties',{}),tile.get('nativeKind'))
    if not archetype:return None
    name=tile['name'];p=tile.get('properties',{});type_id=registry['archetypes'][archetype]['defaultType'];basis='Archetype default'
    family=next((row for row in bindings['families'] if row['archetype']==archetype and row['tileset']==name.rsplit('_',1)[0]),None)
    if family:type_id=family['type'];basis='Family dictionary'
    from archetype_migration import native_type,native_opening,native_evidence
    migrated=native_type(tile,registry,archetype)
    if migrated:type_id,basis=migrated
    if archetype=='boundary' and any(k in p for k in ('HoppableN','HoppableW','FenceTypeLow')):
        type_id='boundary.fence';basis='Native low boundary metadata'
    native_identity=type_id
    if name in bindings['types']:type_id=bindings['types'][name];basis='Tile dictionary'
    requested_type=type_id
    if not any(t['id']==type_id for t in registry['types']) and type_id!='archetype.'+archetype:
        type_id='archetype.'+archetype;basis='Archetype implementation fallback'
    if type_archetype(registry,type_id)!=archetype:raise ValueError('Type dictionary changes native archetype: '+name)
    material_book=material_book or {};material_id=material_book.get('tiles',{}).get(name)
    materials=dict(front=material_id) if material_id else {}
    materials.update(bindings['materials'].get(name,{}))
    default_opening=native_opening(tile,registry) or (dict(archetype=archetype,type=native_identity) if archetype in ('window','door') else source_opening(p,archetype))
    evidence=native_evidence(tile,archetype,registry,(default_opening or {}).get('type',native_identity))
    states={evidence['spriteState']:name,**tile.get('states',{})}
    return dict(name=name,archetype=archetype,type=type_id,basis=basis,orientation=tile.get('orientation'),
                image=tile.get('image'),nativeStates=states,properties=p,materials=materials,
                opening=bindings['openings'][name] if name in bindings.get('openings',{}) else default_opening,requestedType=requested_type,nativeEvidence=evidence,address='source/'+name)


def load_assignments(path=ASSIGNMENTS):
    if not path.exists():return dict(schema=1,revision=0,types={},materials={},openings={})
    result=json.loads(path.read_text('utf-8'));result.setdefault('openings',{});return result


def save_assignment(payload,registry,sources,palette,path=ASSIGNMENTS):
    if not isinstance(payload,dict):raise ValueError('Invalid assignment')
    source=next((s for s in sources if s['name']==payload.get('source')),None)
    if source is None:raise ValueError('Unknown native source')
    identity=payload.get('type')
    if type_archetype(registry,identity)!=source['archetype']:raise ValueError('Type does not match native archetype')
    materials=payload.get('materials',{})
    if not isinstance(materials,dict) or set(materials)-{'front','back','edge','cut'} or any(value not in palette for value in materials.values()):raise ValueError('Unknown material assignment')
    opening=payload.get('opening',source.get('opening'))
    if opening is not None:
        if not isinstance(opening,dict) or opening.get('archetype') not in ('door','window'):raise ValueError('Invalid opening configuration')
        if opening.get('type') and type_archetype(registry,opening['type'])!=opening['archetype']:raise ValueError('Invalid opening type')
        if opening.get('template') and not any(t['id']==opening['template'] and t['archetype']==opening['archetype'] for t in registry.get('openingTemplates',[])):raise ValueError('Invalid opening template')
    assignments=load_assignments(path);assignments['types'][source['name']]=identity;assignments['materials'][source['name']]=materials
    assignments['openings'][source['name']]=opening
    assignments['revision']+=1;path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(assignments,separators=(',',':')),encoding='utf-8');temporary.replace(path)
    return dict(revision=assignments['revision'],source=dict(source,type=identity,materials=materials,opening=opening,basis='Local tile dictionary',bindingStatus='draft'))


def workbench(include_sources=True):
    from materials import REGISTRY
    paths=[*GEOMETRY.rglob('*.json'),ASSIGNMENTS,REGISTRY]
    if include_sources:paths.append(ROOT/'.local/assets/catalog.json')
    revision=tuple((str(path),path.stat().st_mtime_ns,path.stat().st_size) if path.exists() else (str(path),None,None) for path in sorted(paths))
    return _workbench(include_sources,revision)


@lru_cache(maxsize=2)
def _workbench(include_sources,revision):
    from geometry.profile import descriptor
    from archetype_migration import migrate_registry
    registry=json.loads((GEOMETRY/'archetypes.json').read_text('utf-8'))
    bindings=json.loads((GEOMETRY/'archetype-bindings.json').read_text('utf-8'))
    assignments=load_assignments(ASSIGNMENTS)
    bindings=dict(bindings,types={**bindings['types'],**assignments['types']},materials={**bindings['materials'],**assignments['materials']},openings={**bindings.get('openings',{}),**assignments['openings']})
    registry=migrate_registry(registry)
    from materials import registry as material_registry
    material_book=material_registry()
    palette={**registry['palette'],**{identity:dict(label=entry.get('label',identity.removeprefix('material:')),color=entry.get('color','#b8c1c6')) for identity,entry in material_book['materials'].items()}}
    registry=dict(registry,palette=palette)
    if not include_sources:
        return dict(schema=1,status='laboratory',registry=registry,geometry=descriptor(),sources=[],sourcesLoaded=False,
                    assignmentRevision=assignments['revision'],migration=dict(types=len(registry['types']),attachments=len(registry['attachments']),membership=[]))
    path=ROOT/'.local/assets/catalog.json'
    catalog=json.loads(path.read_text('utf-8'))
    state_children={name for tile in catalog['tiles'] for name in tile.get('states',{}).values()}
    sources=[resolved for tile in catalog['tiles'] if tile['name'] not in state_children
             and (resolved:=resolve_source(tile,registry,bindings,material_book))]
    for source in sources:
        if source['name'] in assignments['types']:
            source['bindingStatus']='draft'
            if source['type']==assignments['types'][source['name']]:source['basis']='Local tile dictionary'
    from collections import Counter
    coverage=Counter((s['archetype'],s['type']) for s in sources)
    migration=dict(types=len(registry['types']),attachments=len(registry['attachments']),sources={kind:sum(count for (k,_),count in coverage.items() if k==kind) for kind in ('boundary','window','door')},
                   membership=[dict(archetype=kind,type=identity,count=count) for (kind,identity),count in sorted(coverage.items())],
                   baselines=[s['name'] for s in sources if s['archetype'] in ('window','door') and ('baseline' in s['basis'].lower() or s['basis']=='Archetype default')],
                   unresolvedDoors=[s['name'] for s in sources if s['archetype']=='door' and s['type']=='door.standard'])
    return dict(schema=1,status='laboratory',registry=registry,geometry=descriptor(),sources=sources,sourcesLoaded=True,migration=migration,
                sourceId=catalog['sourceId'],bindingsSource='tools/laboratory/geometry/archetype-bindings.json',
                assignmentRevision=assignments['revision'],assignmentStorage='tools/window-lab/.local/archetype-assignments.json',
                materialSource='tools/materials-lab/materials.json',concept='docs/world-archetypes.md')
