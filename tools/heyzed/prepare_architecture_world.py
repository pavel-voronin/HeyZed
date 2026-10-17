"""Incremental native Unreal resource/World Partition authoring, without a play run."""
from pathlib import Path
import gzip
import hashlib
import inspect
import json
import os
import sys
import time
import unreal

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import texture_policy
from architecture_palette import rgb

DATA=ROOT/'.local/architecture-world'
ASSETS='/Game/HeyZed/Architecture'

def checkpoint_json(path,value):
    pending=path.with_suffix('.pending')
    pending.write_bytes(json.dumps(value,separators=(',',':')).encode())
    for attempt in range(40):
        try:
            pending.replace(path)
            return
        except PermissionError:
            if attempt==39:raise
            time.sleep(.05 if attempt<10 else .25)

tools=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary

def digest(value):return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def saved(asset):return unreal.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
def create(name,folder,cls,factory):
    path=folder+'/'+name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name,folder,cls,factory)
def linear(value):return value/12.92 if value<=.04045 else ((value+.055)/1.055)**2.4

def texture(record,kind):
    path=ROOT/'architecture/materials'/record['file'];name=path.stem
    asset=unreal.load_asset(ASSETS+'/Textures/'+name) if unreal.EditorAssetLibrary.does_asset_exist(ASSETS+'/Textures/'+name) else None
    if asset and unreal.EditorAssetLibrary.get_metadata_tag(asset,'SourceSHA256')==record['sha256']:return asset
    if hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:raise RuntimeError('Material source differs: '+str(path))
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(path),destination_path=ASSETS+'/Textures',destination_name=name,automated=True,save=True,replace_existing=True).items():task.set_editor_property(key,value)
    tools.import_asset_tasks([task]);asset=unreal.load_asset(ASSETS+'/Textures/'+name)
    texture_policy.surface_texture(asset,kind)
    unreal.EditorAssetLibrary.set_metadata_tag(asset,'SourceSHA256',record['sha256']);saved(asset);return asset

def master(maps):
    name='M_ConstructionSurface';asset=create(name,ASSETS+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    recipe=hashlib.sha256(inspect.getsource(master).encode()).hexdigest()
    if unreal.EditorAssetLibrary.get_metadata_tag(asset,'ConstructionShader')==recipe:return asset
    edit.delete_all_material_expressions(asset)
    def node(cls):return edit.create_material_expression(asset,cls)
    def connect(a,b,pin,out=''):
        if not edit.connect_material_expressions(a,out,b,pin):raise RuntimeError('Construction material connection failed: '+str(b.get_class().get_name())+'.'+pin)
    def constant(value):
        result=node(unreal.MaterialExpressionConstant);result.set_editor_property('r',value);return result
    def scalar(name,value):
        result=node(unreal.MaterialExpressionScalarParameter);result.set_editor_property('parameter_name',name);result.set_editor_property('default_value',value);return result
    def custom_data(index):
        value=node(unreal.MaterialExpressionPerInstanceCustomData);value.set_editor_property('data_index',index)
        result=node(unreal.MaterialExpressionVertexInterpolator);connect(value,result,'VS');return result
    uv=node(unreal.MaterialExpressionTextureCoordinate)
    offset=node(unreal.MaterialExpressionAppendVector);connect(custom_data(0),offset,'A');connect(custom_data(1),offset,'B')
    added=node(unreal.MaterialExpressionAdd);connect(uv,added,'A');connect(offset,added,'B')
    scale=node(unreal.MaterialExpressionDivide);connect(added,scale,'A');connect(scalar('TextureMetres',2),scale,'B')
    samples={}
    for kind in ('diff','nor_dx','rough'):
        sample=node(unreal.MaterialExpressionTextureSampleParameter2D);sample.set_editor_property('parameter_name',kind);sample.set_editor_property('texture',maps[kind])
        sample.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='nor_dx' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind=='diff' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        connect(scale,sample,'UVs');samples[kind]=sample
    tint=node(unreal.MaterialExpressionVectorParameter);tint.set_editor_property('parameter_name','Tint');tint.set_editor_property('default_value',unreal.LinearColor(1,1,1,1))
    color=node(unreal.MaterialExpressionMultiply);connect(samples['diff'],color,'A','RGB');connect(tint,color,'B')
    position=node(unreal.MaterialExpressionWorldPosition);vertex_normal=node(unreal.MaterialExpressionVertexNormalWS)
    normal=node(unreal.MaterialExpressionVertexInterpolator);connect(vertex_normal,normal,'VS')
    dirt=node(unreal.MaterialExpressionCustom)
    dirt.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    dirt.set_editor_property('code','''float3 p=P*.01;
float wave=sin(p.x*2.1+sin(p.y*1.7))+sin(p.y*3.2+p.z*.8)+sin(p.z*4.3+p.x*.9);
float coarse=smoothstep(-.45,1.65,wave);
float floorFace=step(.7,abs(N.z));
float height=frac(max(p.z,0)/2.4494900703430176)*2.4494900703430176;
float edge=1-smoothstep(.08,.65,height);
return saturate(lerp(WallDirt*(.2+.8*edge),FloorDirt,floorFace)*coarse*.7);''')
    inputs=[]
    for name in ('P','N','FloorDirt','WallDirt'):
        item=unreal.CustomInput();item.set_editor_property('input_name',name);inputs.append(item)
    dirt.set_editor_property('inputs',inputs)
    connect(position,dirt,'P');connect(normal,dirt,'N');connect(custom_data(2),dirt,'FloorDirt');connect(custom_data(3),dirt,'WallDirt')
    dark=node(unreal.MaterialExpressionMultiply);connect(color,dark,'A');connect(constant(.28),dark,'B')
    blend=node(unreal.MaterialExpressionLinearInterpolate);connect(color,blend,'A');connect(dark,blend,'B');connect(dirt,blend,'Alpha')
    edit.connect_material_property(blend,'',unreal.MaterialProperty.MP_BASE_COLOR)
    edit.connect_material_property(samples['nor_dx'],'RGB',unreal.MaterialProperty.MP_NORMAL)
    rough=node(unreal.MaterialExpressionLinearInterpolate);connect(samples['rough'],rough,'A','R');connect(constant(1),rough,'B');connect(dirt,rough,'Alpha')
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.connect_material_property(scalar('Metallic',0),'',unreal.MaterialProperty.MP_METALLIC)
    edit.set_base_material_usage(asset,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES,True)
    edit.recompile_material(asset);unreal.EditorAssetLibrary.set_metadata_tag(asset,'ConstructionShader',recipe);saved(asset);return asset

def surface_usage(parent):
    usage=unreal.MaterialUsage.MATUSAGE_NANITE
    if parent and not edit.has_material_usage(parent,usage):
        edit.set_base_material_usage(parent,usage,True)
        saved(parent)

def resources():
    surface_usage(unreal.load_asset(ASSETS+'/Materials/M_ConstructionSurface'))
    factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.HeyZedArchitectureResources)
    resource=create('Resources',ASSETS,unreal.HeyZedArchitectureResources,factory)
    import architecture_facing
    architecture_facing.publish(resource)
    library=DATA/'library/library.json';content=library.read_text('utf-8')
    fingerprint=hashlib.sha256(content.encode()).hexdigest()
    settings=hashlib.sha256((ROOT/'architecture/materials/sources.json').read_bytes()+(DATA/'palette.json').read_bytes()+
                            (DATA/'definitions.json').read_bytes()+inspect.getsource(master).encode()+fingerprint.encode()).hexdigest()
    if unreal.EditorAssetLibrary.get_metadata_tag(resource,'ConstructionResources')==settings:return resource
    manifest=json.loads((ROOT/'architecture/materials/sources.json').read_text('utf-8'))
    maps={entry['id']:{kind:texture(record,kind) for kind,record in entry['maps'].items()} for entry in manifest['assets']}
    for textures in maps.values():texture_policy.surface_maps(textures)
    parent=master(maps['plastered_wall_02'])
    surface_usage(parent)
    palette=json.loads((DATA/'palette.json').read_text('utf-8'));materials={}
    for name,spec in palette['palette'].items():
        instance=create('MI_'+name,ASSETS+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(instance,parent)
        for kind,tex in maps[spec['asset']].items():edit.set_material_instance_texture_parameter_value(instance,kind,tex)
        # The downloaded diffuse supplies structure; shared tints supply the
        # provisional native finish colour without per-tile textures.
        edit.set_material_instance_vector_parameter_value(instance,'Tint',unreal.LinearColor(*spec['tint'],1))
        edit.set_material_instance_scalar_parameter_value(instance,'TextureMetres',spec['width'])
        edit.set_material_instance_scalar_parameter_value(instance,'Metallic',spec['metallic']);saved(instance);materials[name]=instance
    from prepare_building import material
    materials['Glass']=unreal.load_asset(ASSETS+'/Materials/M_Glass') or material('Glass',None,(.65,.82,.85))
    resource.set_editor_property('materials',materials);resource.set_editor_property('surface_bindings',palette['bindings'])
    source=json.loads((DATA/'definitions.json').read_text('utf-8'))
    resource.set_editor_property('source_bindings',{k:json.dumps(v,separators=(',',':')) for k,v in source['sources'].items()})
    if unreal.EditorAssetLibrary.get_metadata_tag(resource,'ConstructionLibrary')!=fingerprint:
        if not resource.prepare_library(content,str(library.parent)):raise RuntimeError('Native construction resource authoring failed')
        unreal.EditorAssetLibrary.set_metadata_tag(resource,'ConstructionLibrary',fingerprint)
    unreal.EditorAssetLibrary.set_metadata_tag(resource,'ConstructionResources',settings)
    saved(resource);return resource

def distant_materials():
    asset=create('M_ConstructionDistant',ASSETS+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    recipe=hashlib.sha256(inspect.getsource(distant_materials).encode()).hexdigest()
    if unreal.EditorAssetLibrary.get_metadata_tag(asset,'EnvelopeShader')!=recipe:
        edit.delete_all_material_expressions(asset)
        def node(cls):return edit.create_material_expression(asset,cls)
        def connect(a,b,pin,out=''):
            if not edit.connect_material_expressions(a,out,b,pin):raise RuntimeError('Distant material connection failed: '+pin)
        position=node(unreal.MaterialExpressionWorldPosition);normal=node(unreal.MaterialExpressionPixelNormalWS)
        width=node(unreal.MaterialExpressionScalarParameter);width.set_editor_property('parameter_name','TextureMetres');width.set_editor_property('default_value',2)
        uv=node(unreal.MaterialExpressionCustom);uv.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT2)
        uv.set_editor_property('code','float3 n=abs(N);return (n.z>.5?P.xy:(n.x>n.y?P.yz:P.xz))*.01/max(Scale,.01);')
        inputs=[]
        for name in ('P','N','Scale'):
            item=unreal.CustomInput();item.set_editor_property('input_name',name);inputs.append(item)
        uv.set_editor_property('inputs',inputs);connect(position,uv,'P');connect(normal,uv,'N');connect(width,uv,'Scale')
        sample=node(unreal.MaterialExpressionTextureSampleParameter2D);sample.set_editor_property('parameter_name','Diffuse')
        default_texture=unreal.load_asset(ASSETS+'/Textures/plastered_wall_02_diff')
        if not default_texture:raise RuntimeError('Shared distant default diffuse is unavailable')
        sample.set_editor_property('texture',default_texture)
        sample.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR);connect(uv,sample,'UVs')
        tint=node(unreal.MaterialExpressionVectorParameter);tint.set_editor_property('parameter_name','Tint');tint.set_editor_property('default_value',unreal.LinearColor(1,1,1,1))
        color=node(unreal.MaterialExpressionMultiply);connect(sample,color,'A','RGB');connect(tint,color,'B')
        rough=node(unreal.MaterialExpressionConstant);rough.set_editor_property('r',.9)
        if not edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR):raise RuntimeError('Distant base color connection failed')
        if not edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS):raise RuntimeError('Distant roughness connection failed')
        edit.set_base_material_usage(asset,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES,True)
        edit.recompile_material(asset);unreal.EditorAssetLibrary.set_metadata_tag(asset,'EnvelopeShader',recipe);saved(asset)
    palette=json.loads((DATA/'palette.json').read_text('utf-8'))
    manifest=json.loads((ROOT/'architecture/materials/sources.json').read_text('utf-8'))
    maps={entry['id']:entry['maps']['diff'] for entry in manifest['assets']}
    instances={}
    for name,spec in palette['palette'].items():
        instance=create('MI_Distant_'+name,ASSETS+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(instance,asset)
        record=maps[spec['asset']];diffuse=unreal.load_asset(ASSETS+'/Textures/'+Path(record['file']).stem)
        if not diffuse:raise RuntimeError('Shared distant diffuse is unavailable: '+record['file'])
        edit.set_material_instance_texture_parameter_value(instance,'Diffuse',diffuse)
        edit.set_material_instance_vector_parameter_value(instance,'Tint',unreal.LinearColor(*spec['tint'],1))
        edit.set_material_instance_scalar_parameter_value(instance,'TextureMetres',spec['width']);saved(instance);instances[name]=instance
    return {finish:instances[name] for finish,name in palette['bindings'].items()},instances['Plaster_Light']

def far_layers():
    builder=unreal.load_class(None,'/Script/HeyZedAuthoring.HeyZedArchitectureHLOD')
    if not builder:raise RuntimeError('Construction HLOD editor module is unavailable')
    materials,default=distant_materials()
    far=create('ArchitectureFar',ASSETS,unreal.HLODLayer,unreal.HLODLayerFactory())
    city=create('ArchitectureCity',ASSETS,unreal.HLODLayer,unreal.HLODLayerFactory())
    for layer,size,distance in ((far,51200,409600),(city,204800,3200000)):
        layer.set_editor_property('layer_type',unreal.HLODLayerType.CUSTOM)
        layer.set_editor_property('hlod_builder_class',builder)
        layer.set_editor_property('cell_size',size);layer.set_editor_property('loading_range',distance)
        settings=layer.get_editor_property('hlod_builder_settings')
        settings.set_editor_property('BoxMesh',unreal.load_asset('/Engine/BasicShapes/Cube'))
        settings.set_editor_property('ExteriorMaterials',materials)
        settings.set_editor_property('DefaultMaterial',default)
    far.set_editor_property('parent_layer',city)
    saved(far);saved(city);return far

def floor_masks(actors,descriptors):
    regions={str(d.label).removeprefix('heyzed base '):d.guid for d in descriptors if str(d.label).startswith('heyzed base ')}
    folder=DATA/'resolved/floor-masks';checkpoint=DATA/'floor-mask-authoring.json'
    done=json.loads(checkpoint.read_text('utf-8')) if checkpoint.exists() else {}
    pending=[(path,hashlib.sha256(path.read_bytes()).hexdigest()) for path in sorted(folder.glob('*.bin'))]
    for start in range(0,len(pending),32):
        batch=[(p,h) for p,h in pending[start:start+32] if done.get(p.stem)!=h]
        if not batch:continue
        for path,_ in batch:
            if path.stem not in regions:raise RuntimeError('Missing native base actor for architecture floor: '+path.stem)
        guids=[regions[p.stem] for p,_ in batch];unreal.WorldPartitionBlueprintLibrary.load_actors(guids)
        loaded={a.get_actor_label().removeprefix('heyzed base '):a for a in actors.get_all_level_actors() if isinstance(a,unreal.HeyZedBakedRegion)}
        for path,fingerprint in batch:
            if not loaded[path.stem].apply_architecture_mask(str(path)):raise RuntimeError('Architecture floor mask publication failed: '+path.stem)
        if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True):raise RuntimeError('Architecture floor mask save failed')
        done.update({p.stem:h for p,h in batch});checkpoint.write_bytes(json.dumps(done).encode())
        loaded.clear();unreal.WorldPartitionBlueprintLibrary.unload_actors(guids);unreal.SystemLibrary.collect_garbage()
    unreal.log('heyzed architecture floor masks: '+str(len(done)))

def buildings(resource):
    inventory=json.loads((DATA/'resolved/inventory.json').read_text('utf-8'))
    if not inventory['complete']:raise RuntimeError('Construction catalog is incomplete')
    target=json.loads((ROOT/'client/Saved/HeyZed/baked-world.json').read_text('utf-8'))
    unreal.EditorLoadingAndSavingUtils.load_map(target['world'])
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not unreal.HeyZedArchitecture.configure_world(world):raise RuntimeError('Architecture streaming grid is unavailable')
    selected_ids=set(filter(None,os.environ.get('HEYZED_ARCHITECTURE_IDS','').split(',')))
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);layer=unreal.load_asset(ASSETS+'/ArchitectureFar') if selected_ids else far_layers()
    if not layer:raise RuntimeError('Existing architecture HLOD layer is required for targeted authoring')
    descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    if isinstance(descs,tuple) and isinstance(descs[0],bool):descs=descs[1]
    existing={str(d.label):d for d in descs}
    if not selected_ids:floor_masks(actors,descs)
    checkpoint=DATA/'authoring.json';state=json.loads(checkpoint.read_text('utf-8')) if checkpoint.exists() else {}
    library=json.loads((DATA/'library/library.json').read_text('utf-8'))['definitions']
    definition_hashes={key:digest(value) for key,value in library.items()}
    palette=json.loads((DATA/'palette.json').read_text('utf-8'))['bindings']
    limit=int(os.environ.get('HEYZED_ARCHITECTURE_LIMIT','0'));completed=0
    entries=sorted(inventory['buildings'],key=lambda e:(0 if e['id']=='31_45_25' else 1,e['id']))
    if selected_ids:
        entries=[entry for entry in entries if entry['id'] in selected_ids]
        if {entry['id'] for entry in entries}!=selected_ids:raise RuntimeError('Unknown targeted building IDs')
    complete=not selected_ids
    for entry in entries:
        if (DATA/'pause.request').exists():
            (DATA/'publication.json').write_bytes(json.dumps(dict(state='paused',completed=len(state),total=len(entries))).encode())
            unreal.log('heyzed architecture publication paused at a completed building');complete=False;break
        facts=json.loads(gzip.decompress(Path(entry['file']).read_bytes()))
        if 'interior' not in facts:
            raise RuntimeError('Prepare native room facts before Unreal authoring: '+entry['id'])
        cells={tuple(cell['address']):cell for cell in facts['cells']}
        for point in facts['interior']:
            cells.setdefault(tuple(point),dict(address=point,floor=None,walls=[],grime=[]))
        facts['cells']=[cells[point] for point in sorted(cells)]
        used={'boundary.wall'}
        for cell in facts['cells']:
            local={'boundary.wall'};surfaces={cell.get('floor')};pose_revision=False
            for wall in cell['walls']:
                local.update(filter(None,(wall.get('insert'),wall.get('boundary'),wall.get('openingConfig',{}).get('type'))))
                local.update(a['definition'] for a in wall.get('attachments',[]))
                surfaces.update((wall.get('surfaceA'),wall.get('surfaceB')))
                definition=library.get(wall.get('insert'),{})
                states=definition.get('states',{});opened=states.get('open',[])
                hinged=definition.get('kind')=='door' and any(p.get('motion',{}).get('angle') for p in opened)
                selected=states.get('closed' if hinged else wall.get('state','closed'),[])
                pose_revision|=any(p.get('motion',{}).get('angle') for p in selected) or (hinged and (str(wall.get('insert','')).startswith('door.gate') or definition.get('cells',1)>1))
            used.update(local)
            recipe=dict(definitions={k:definition_hashes.get(k) for k in sorted(local)},
                        surfaces={k:palette.get(k) for k in sorted(filter(None,surfaces))},factory=5)
            if pose_revision:recipe['poseFactory']=3
            cell['renderRecipe']=digest(recipe)
        fingerprint=digest(dict(cells=facts['cells'],metric=facts['metric'],interior=facts['interior'],factory=5))
        label='Architecture_'+entry['id'];desc=existing.get(label)
        if desc and state.get(entry['id'])==fingerprint:continue
        if desc:unreal.WorldPartitionBlueprintLibrary.load_actors([desc.guid])
        actor=next((a for a in actors.get_all_level_actors() if isinstance(a,unreal.HeyZedArchitecture) and a.get_actor_label()==label),None)
        actor=actor or actors.spawn_actor_from_class(unreal.HeyZedArchitecture,unreal.Vector(entry['bounds'][0]*100,entry['bounds'][1]*100,0))
        actor.set_actor_label(label);actor.set_editor_property('resources',resource);actor.set_editor_property('hlod_layer',layer)
        if not actor.prepare(json.dumps(facts,separators=(',',':'))):raise RuntimeError('Construction authoring failed: '+entry['id'])
        if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True):raise RuntimeError('Construction save failed: '+entry['id'])
        unreal.EditorAssetLibrary.save_loaded_asset(resource,only_if_is_dirty=True);state[entry['id']]=fingerprint
        checkpoint_json(checkpoint,state)
        guid=actor.get_editor_property('actor_guid');unreal.WorldPartitionBlueprintLibrary.unload_actors([guid])
        del actor;completed+=1
        if completed%20==0:unreal.SystemLibrary.collect_garbage()
        unreal.log('heyzed world architecture: '+str(len(state))+'/'+str(len(entries))+'; '+entry['id'])
        if limit and completed>=limit:complete=False;break
    saved(resource);unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    if complete:
        (DATA/'publication.json').write_bytes(json.dumps(dict(state='complete',completed=len(entries),total=len(entries),world=target['world'],library=digest(library))).encode())

def attachment_mounts():
    inventory=json.loads((DATA/'resolved/inventory.json').read_text('utf-8'))
    target=json.loads((ROOT/'client/Saved/HeyZed/baked-world.json').read_text('utf-8'))
    unreal.EditorLoadingAndSavingUtils.load_map(target['world'])
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    if isinstance(descs,tuple) and isinstance(descs[0],bool):descs=descs[1]
    existing={str(d.label):d for d in descs}
    selected=set(filter(None,os.environ.get('HEYZED_ARCHITECTURE_IDS','').split(',')))
    completed=mounts=visited=0
    for entry in inventory['buildings']:
        if selected and entry['id'] not in selected:continue
        facts=json.loads(gzip.decompress(Path(entry['file']).read_bytes()))
        def key(cell,wall,a):
            side=a.get('edgeSide','A' if a.get('side')=='outside' else 'B')
            return (tuple(cell['address']),wall['axis'],a['family'],side,tuple(sorted(a['sources'])))
        fresh={key(c,w,a):a for c in facts['cells'] for w in c['walls'] for a in w.get('attachments',[]) if 'mountCell' in a}
        if not fresh:continue
        label='Architecture_'+entry['id'];desc=existing.get(label)
        if not desc:raise RuntimeError('Missing prepared architecture: '+label)
        unreal.WorldPartitionBlueprintLibrary.load_actors([desc.guid])
        actor=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.HeyZedArchitecture) and a.get_actor_label()==label)
        previous=json.loads(actor.get_editor_property('description'));changed=0
        for cell in previous['cells']:
            for wall in cell['walls']:
                for attachment in wall.get('attachments',[]):
                    source=fresh.get(key(cell,wall,attachment))
                    if source is None:continue
                    before=dict(attachment)
                    for field in ('mountCell','edgeSide','prototypeNormal'):attachment[field]=source[field]
                    attachment.pop('side',None)
                    changed+=before!=attachment
        if changed:
            if not actor.prepare(json.dumps(previous,separators=(',',':'))):raise RuntimeError('Attachment authoring failed: '+label)
            if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True):raise RuntimeError('Attachment save failed: '+label)
            completed+=1;mounts+=changed
            unreal.log('heyzed attachment mounts: '+str(completed)+' buildings; '+str(mounts)+' coverings; '+entry['id'])
        unreal.WorldPartitionBlueprintLibrary.unload_actors([desc.guid]);del actor
        visited+=1
        if visited%20==0:unreal.SystemLibrary.collect_garbage()
    unreal.log('heyzed attachment mounts complete: '+str(completed)+' buildings; '+str(mounts)+' coverings')


def main():
    if os.environ.get('HEYZED_ARCHITECTURE_PHASE')=='attachments':
        attachment_mounts();return
    resource=resources()
    if os.environ.get('HEYZED_ARCHITECTURE_PHASE','world')=='resources':far_layers()
    else:buildings(resource)

if __name__=='__main__':main()
