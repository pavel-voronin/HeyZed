"""Reuse the existing geometry and native classifiers without baking combinations."""
from copy import deepcopy
from functools import lru_cache
from pathlib import Path

PROFILE_SOURCE=Path(__file__).resolve().parents[1]/'laboratory/geometry/aperture_profiles.json'


@lru_cache(maxsize=1)
def window_lookup(revision):
    from geometry.apertures import profiles
    by_name={};by_shape={}
    for profile in profiles():
        if profile.get('selector'):by_shape[profile['selector']['value']]=profile['id']
        for binding in profile['bindings'].values():
            for group in binding.get('tileGroups',[]):
                for index in group['indices']:by_name[group['tileset']+'_'+str(index)]=profile['id']
    return by_name,by_shape


def window_identity(tile):
    by_name,by_shape=window_lookup(PROFILE_SOURCE.stat().st_mtime_ns)
    p=tile.get('properties',{})
    return by_shape.get(p.get('WindowShape')) if any(k in p for k in ('wall','cutN','cutW','DoorWallN','DoorWallW')) and p.get('WindowShape') in by_shape else by_name.get(tile['name'])


def migrate_registry(registry):
    from geometry.apertures import profiles
    from geometry.openings import sections, section_geometry
    from geometry.constructions import families, family_geometry, partial_door_geometry
    from geometry.door_styles import descriptor as door_styles
    from geometry.gates import LABELS, gate_profile
    from geometry.attachments import families as attachments, native_rules
    result=deepcopy(registry);types=result['types'];result['openingTemplates']=[]
    types.append(dict(id='boundary.logs',archetype='boundary',extends='boundary.wall',label='Log wall',generator='logs',material='wood',status='migrated-section',modelStatus='candidate'))
    def add(identity,kind,label,aperture,**values):
        types.append(dict(id=identity,archetype=kind,extends=kind+'.standard',label=label,
            dimensions=dict(shape=aperture['shape'],points=aperture['points']),
            aperture={k:v for k,v in aperture.items() if k not in ('points','bounds','tiles','windows')},
            status='migrated-model',**values))
    for p in profiles():
        add('type.'+p['id'],'window',p['label'],p,generator='approved-window',recipe=None,legacyProfile=p['id'],
            stateAliases={} if p['id'] in ('window-9-0','window-9-1','window-10-0') else dict(open='closed'),
            modelStatus='approved' if p['id'] in ('window-9-0','window-9-1') else 'candidate')
    for section in sections():
        if section['geometryStatus']!='approved':continue
        aperture=section_geometry(section['id'])
        if section['kind']=='window':
            add('type.'+section['id'],'window',section['label'],aperture,generator='fixed-window',recipe=None,stateAliases=dict(open='closed'),modelStatus='candidate')
        else:
            for style in door_styles()['styles']:
                identity='door.'+style['id'] if section['id']=='opening.door.01' else 'door.'+section['id']+'.'+style['id']
                slots=['barricade']+(['sheet'] if style['kind']!='solid' else [])
                add(identity,'door',section['label']+' | '+style['label'],aperture,generator='door',style=style['id'],slots=slots,modelStatus='candidate')
    add('door.partial','door','Short door leaf',partial_door_geometry(),generator='door',style='solid',slots=['barricade'],modelStatus='approved')
    for family in families():
        kind=family['kind']
        if kind in ('open-passage','tall-passage'):
            aperture=family_geometry(family);result['openingTemplates'].append(dict(id='opening.'+family['id'],label=family['label'],archetype='door',dimensions=dict(shape=aperture['shape'],points=aperture['points']),status='migrated-geometry'));continue
        if kind=='garage':continue
        aperture=family_geometry(family)
        slots=[] if kind in ('sliding-door','double-door') else ['barricade']
        add('door.'+family['id'],'door',family['label'],aperture,generator=kind,style='divided-glazed' if kind=='double-door' else 'solid',slots=slots,cells=family.get('cells',1),modelStatus='approved')
    for identity,label in LABELS.items():
        props={'DoubleDoor':'1'} if 'double' in identity or identity=='farm' else {}
        aperture=gate_profile('fixtures_doors_fences_01_0',props)
        # The geometry provider consumes its declared design; native membership is
        # resolved separately by the original gate classifier below.
        low=identity.endswith('-low') or identity in ('wood-painted-low','farm','log')
        from geometry.profile import resolve
        p=resolve();width=(4 if props else 1)*p['tileSize'];height=p['lowWallHeight'] if low else p['doorTop']
        aperture.update(id='gate-'+identity,points=[[0,0],[0,height],[width,height],[width,0]],
            construction=dict(mechanism='hinged-gate',gateDesign=identity,double=bool(props),hinge='left',openAngleDegrees=90))
        add('door.gate.'+identity,'door',label,aperture,generator='native-gate',slots=[],cells=4 if props else 1,modelStatus='approved')
    from geometry.profile import resolve
    p=resolve()
    for cells in (1,2,3):
        width=cells*p['tileSize'];rail=p['doorRailWidth'];clear=p['floorHeight']-p['slabDepth']-2*rail-p['doorThickness']-p['doorLeafClearance']
        aperture=dict(id='garage-'+str(cells),shape='rectangle',points=[[p['wallThickness']-rail/2,0],[p['wallThickness']-rail/2,clear+rail],[width-p['wallThickness']+rail/2,clear+rail],[width-p['wallThickness']+rail/2,0]],joins=[])
        add('door.garage.'+str(cells),'door','Garage | '+str(cells)+' cells',aperture,generator='garage',slots=[],cells=cells,modelStatus='approved' if cells==3 else 'baseline')
    # Full-height glazing without a native profile uses a declared baseline,
    # while missing shape metadata remains explicitly visible in the audit.
    for identity,label,dimensions in [('window.fixed','Fixed window','window.standard'),('window.full-glazing','Full-height glazing','window.full-cell'),('window.patio-fixed','Patio fixed light','window.full-cell')]:
        types.append(dict(id=identity,archetype='window',extends='window.storefront' if dimensions=='window.full-cell' else 'window.standard',label=label,generator='approved-window',recipe='fixed-section',dimensions=dimensions,stateAliases=dict(open='closed'),**({} if identity=='window.fixed' else {'slots':[]}),status='baseline',modelStatus='baseline'))
    # Preserve the four existing storefront proposals and their original model
    # settings; they are proposals, not newly approved geometry.
    for role,reverse in [('edge',False),('middle',False),('corner',False),('corner-reverse',True)]:
        types.append(dict(id='window.storefront.'+role,archetype='window',extends='window.storefront',label='Storefront | '+role,
            generator='approved-window',recipe='storefront',reviewRecipe='storefront-corner' if role.startswith('corner') else 'storefront',cornerReverse=reverse,slots=[],status='candidate',modelStatus='candidate'))
    result['archetypes']['window']['slots']=['shutters','curtain','roller','sheet','barricade','rope']
    result['attachments']=deepcopy(attachments())
    result['nativeAttachmentRules']=native_rules()
    return result


def native_type(tile,registry,archetype):
    if archetype=='boundary':
        return ('boundary.logs','Existing log section; boundary model pending visual review') if tile['name'].startswith('walls_logs_') and any(t['id']=='boundary.logs' for t in registry['types']) else None
    from geometry.constructions import families, tile_profile
    from geometry.openings import section_for_tile
    from geometry.door_styles import classify
    from geometry.gates import design
    name=tile['name'];p=tile.get('properties',{});known={t['id'] for t in registry['types']}
    def choice(identity,basis):return (identity,basis) if identity in known else None
    if archetype=='window':
        if p.get('DoorSound')=='SlidingGlassDoor':return choice('window.patio-fixed','Native sliding assembly fixed light')
        profile=tile_profile(name,p,tile.get('orientation')) if not p.get('WindowShape') or 'type.window-'+p['WindowShape']+'-0' in known else None
        if profile and 'type.'+profile['id'] in known:return choice('type.'+profile['id'],'Reviewed native window profile')
        if name.startswith('walls_commercial_02_'):
            index=int(name.rsplit('_',1)[1]);role={48:'edge',53:'edge',49:'middle',52:'middle',50:'corner',51:'corner',54:'corner-reverse',55:'corner-reverse'}.get(index)
            if role:return choice('window.storefront.'+role,'Existing storefront proposal dictionary')
        if 'WindowLocked' in p or 'open' not in tile.get('states',{}):
            return choice('window.full-glazing' if p.get('GrimeType')=='FullWindow' else 'window.fixed','Native fixed glazing metadata; baseline dimensions')
    if archetype=='door':
        gate=design(name,p)
        if gate:return choice('door.gate.'+gate,'Native gate design')
        if 'GarageDoor' in p:return choice('door.garage.3','Native garage role; reviewed three-cell assembly')
        if p.get('DoorSound')=='SlidingGlassDoor':return choice('door.door-sliding-glass','Native sliding glass assembly')
        sheet,_,index=name.rpartition('_')
        if sheet=='fixtures_doors_02' and index.isdigit() and 16<=int(index)<=27:return choice('door.partial','Reviewed short door leaf')
        for family in families():
            if family['kind'] in ('cubicle','damaged') and name in family.get('insertTiles',[]):return choice('door.'+family['id'],'Reviewed construction dictionary')
        style=classify(tile)
        if style and style.get('typeId'):return choice('door.'+style['typeId'],'Native door appearance classifier')
    if archetype=='boundary':
        return None
    return None


def native_opening(tile,registry):
    p=tile.get('properties',{})
    if not any(k in p for k in ('WindowShape','WindowN','WindowW','cutN','cutW','DoorWallN','DoorWallW','doorFrN','doorFrW')):return None
    from geometry.apertures import profile_for_tile
    from geometry.openings import section_for_tile
    from geometry.constructions import families
    p=tile.get('properties',{});name=tile['name'];known={t['id'] for t in registry['types']}
    identity=window_identity(tile)
    if identity and 'type.'+identity in known:return dict(archetype='window',type='type.'+identity)
    section=section_for_tile(name)
    if section and section['geometryStatus']=='approved':
        identity='type.'+section['id'] if section['kind']=='window' else 'door.solid' if section['id']=='opening.door.01' else 'door.'+section['id']+'.solid'
        if identity in known:return dict(archetype=section['kind'],type=identity)
    for family in families():
        if name in family['wallTiles']:
            if family['kind'] in ('open-passage','tall-passage'):return dict(archetype='door',template='opening.'+family['id'])
            identity='door.'+family['id']
            if identity in known:return dict(archetype='door',type=identity)
    return None


def native_evidence(tile,archetype,registry=None,identity=None):
    p=tile.get('properties',{});result=dict(spriteState='present' if archetype=='boundary' else 'closed')
    entry=next((t for t in (registry or {}).get('types',[]) if t['id']==identity),{})
    quarter=entry.get('aperture',{}).get('bindings',{}).get(tile.get('orientation'),{}).get('quarterTurns',0)
    result.update(quarterTurns=quarter,mirrorU=quarter>=2)
    if 'GarageDoor' in p:
        role=int(p['GarageDoor']);result.update(spriteState='open' if role>3 else 'closed',assembly=dict(kind='garage',cells=3,role=(role-1)%3+1,required='three actual neighboring sections'))
    elif 'DoubleDoor' in p:
        role=int(p['DoubleDoor']);result.update(spriteState='open' if role>4 else 'closed',assembly=dict(kind='double-gate',cells=4,role=(role-1)%4+1,required='four actual native sections'))
    elif p.get('DoorSound')=='SlidingGlassDoor':
        from geometry.constructions import families
        for family in families():
            if family['kind']!='sliding-door':continue
            for assembly in family['assemblies']:
                for role,name in assembly.items():
                    if role=='side' or name!=tile['name']:continue
                    result.update(spriteState='open' if role=='open' else 'glassRemoved' if role=='empty' else 'closed',assembly=dict(kind='sliding-door',cells=2,role=role,required='moving leaf and its actual fixed-light neighbor'))
    result['attachmentEvidence']=dict(doorSheet=bool('doorTrans' in p and 'GarageDoor' not in p and p.get('DoorSound','').lower()!='slidingglassdoor') if archetype=='door' else None,
        origin='Existing native attachment rules; window attachment eligibility requires the live receiver')
    return result
