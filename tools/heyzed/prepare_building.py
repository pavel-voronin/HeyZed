"""Author one native building and its reusable, volumetric Unreal mesh resources."""
from pathlib import Path
import hashlib
import inspect
import json
import sys
import unreal

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import texture_policy
import material_usage

ASSETS='/Game/HeyZed/Architecture'
PALETTE=dict(Brick=('brick_wall_006',(1,1,1)),Plaster=('plastered_wall_02',(.85,.82,.72)),
             Wood=('wood_planks_dirt',(1,1,1)),Floor=('wood_planks_dirt',(.65,.60,.50)),
             Cut=(None,(.55,.52,.45)),Frame=(None,(.8,.8,.75)),
             Metal=(None,(.3,.32,.33)),Glass=(None,(.65,.82,.85)))
tools=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary


def imported(record,kind):
    source=ROOT/'architecture/materials'/record['file']
    if hashlib.sha256(source.read_bytes()).hexdigest()!=record['sha256']:raise RuntimeError('Architecture source changed: '+source.name)
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(source),destination_path=ASSETS+'/Textures',destination_name=source.stem,automated=True,save=True,replace_existing=True).items():task.set_editor_property(key,value)
    tools.import_asset_tasks([task]);asset=unreal.load_asset(ASSETS+'/Textures/'+source.stem)
    texture_policy.surface_texture(asset,kind);unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def material(name,maps=None,tint=(1,1,1),width=2):
    path=ASSETS+'/Materials/M_'+name
    asset=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_'+name,ASSETS+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(asset)
    def node(cls):return edit.create_material_expression(asset,cls)
    def constant(value):
        n=node(unreal.MaterialExpressionConstant);n.set_editor_property('r',value);return n
    def connect(n,pin):edit.connect_material_property(n,'',pin)
    color=node(unreal.MaterialExpressionConstant3Vector);color.set_editor_property('constant',unreal.LinearColor(*tint))
    if maps:
        offset=[]
        for index in range(2):
            data=node(unreal.MaterialExpressionPerInstanceCustomData);data.set_editor_property('data_index',index);offset.append(data)
        append=node(unreal.MaterialExpressionAppendVector)
        for pin,n in zip(('A','B'),offset):edit.connect_material_expressions(n,'',append,pin)
        interpolator=node(unreal.MaterialExpressionVertexInterpolator);edit.connect_material_expressions(append,'',interpolator,'VS')
        uv=node(unreal.MaterialExpressionTextureCoordinate)
        add=node(unreal.MaterialExpressionAdd);edit.connect_material_expressions(uv,'',add,'A');edit.connect_material_expressions(interpolator,'',add,'B')
        scale=node(unreal.MaterialExpressionMultiply);edit.connect_material_expressions(add,'',scale,'A');edit.connect_material_expressions(constant(1/width),'',scale,'B')
        for kind,prop in [('diff',unreal.MaterialProperty.MP_BASE_COLOR),('nor_dx',unreal.MaterialProperty.MP_NORMAL),('rough',unreal.MaterialProperty.MP_ROUGHNESS)]:
            sample=node(unreal.MaterialExpressionTextureSampleParameter2D);sample.set_editor_property('parameter_name',kind);sample.set_editor_property('texture',maps[kind])
            sample.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='nor_dx' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind=='diff' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
            edit.connect_material_expressions(scale,'',sample,'Coordinates')
            if kind=='diff':
                multiply=node(unreal.MaterialExpressionMultiply);edit.connect_material_expressions(sample,'RGB',multiply,'A');edit.connect_material_expressions(color,'',multiply,'B');connect(multiply,prop)
            else:edit.connect_material_property(sample,'RGB' if kind=='nor_dx' else 'R',prop)
    else:
        connect(color,unreal.MaterialProperty.MP_BASE_COLOR);connect(constant(.8),unreal.MaterialProperty.MP_ROUGHNESS)
    connect(constant(0),unreal.MaterialProperty.MP_METALLIC)
    if name=='Metal':connect(constant(.8),unreal.MaterialProperty.MP_METALLIC)
    if name=='Glass':
        asset.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
        asset.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        connect(constant(.12),unreal.MaterialProperty.MP_OPACITY);connect(constant(.06),unreal.MaterialProperty.MP_ROUGHNESS)
        connect(constant(.8),unreal.MaterialProperty.MP_SPECULAR)
    material_usage.require(asset,nanite=name!='Glass',instanced=True)
    edit.recompile_material(asset);unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def main():
    source_manifest=ROOT/'architecture/materials/sources.json'
    manifest=json.loads(source_manifest.read_text(encoding='utf-8'))
    resource_code=(inspect.getsource(imported)+inspect.getsource(material)).encode('utf-8')
    recipe=hashlib.sha256(resource_code+json.dumps(PALETTE,sort_keys=True).encode('utf-8')+Path(texture_policy.__file__).read_bytes()+source_manifest.read_bytes())
    for item in manifest['assets']:
        for record in item['maps'].values():
            content=(ROOT/'architecture/materials'/record['file']).read_bytes()
            if hashlib.sha256(content).hexdigest()!=record['sha256']:raise RuntimeError('Architecture source changed: '+record['file'])
            recipe.update(content)
    fingerprint=recipe.hexdigest()
    path=ASSETS+'/Resources'
    resources=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not resources:
        factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.HeyZedArchitectureResources)
        resources=tools.create_asset('Resources',ASSETS,unreal.HeyZedArchitectureResources,factory)
    if unreal.EditorAssetLibrary.get_metadata_tag(resources,'ArchitectureResourceRecipe')!=fingerprint:
        textures={}
        for item in manifest['assets']:
            maps={kind:imported(record,kind) for kind,record in item['maps'].items()};texture_policy.surface_maps(maps);textures[item['id']]=maps
        materials={name:material(name,textures.get(source),tint) for name,(source,tint) in PALETTE.items()}
        resources.set_editor_property('materials',materials)
        unreal.EditorAssetLibrary.set_metadata_tag(resources,'ArchitectureResourceRecipe',fingerprint)
    facts=json.loads((ROOT/'.local/architecture/building.json').read_text())
    target=json.loads((ROOT/'client/Saved/HeyZed/baked-world.json').read_text())
    unreal.EditorLoadingAndSavingUtils.load_map(target['world'])
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    label='Architecture_'+facts['id']
    descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    if isinstance(descs,tuple) and isinstance(descs[0],bool):descs=descs[1]
    for desc in descs:
        if str(desc.label)==label:unreal.WorldPartitionBlueprintLibrary.load_actors([desc.guid])
    actor=next((a for a in actors.get_all_level_actors() if isinstance(a,unreal.HeyZedArchitecture) and a.get_actor_label()==label),None)
    actor=actor or actors.spawn_actor_from_class(unreal.HeyZedArchitecture,unreal.Vector(facts['bounds'][0]*100,facts['bounds'][1]*100,0))
    actor.set_actor_label(label);actor.set_editor_property('resources',resources)
    layer_path=ASSETS+'/ArchitectureFar'
    layer=unreal.load_asset(layer_path) if unreal.EditorAssetLibrary.does_asset_exist(layer_path) else tools.create_asset('ArchitectureFar',ASSETS,unreal.HLODLayer,unreal.HLODLayerFactory())
    layer.set_editor_property('layer_type',unreal.HLODLayerType.MESH_SIMPLIFY)
    layer.set_editor_property('cell_size',51200);layer.set_editor_property('loading_range',409600)
    actor.set_editor_property('hlod_layer',layer)
    if not actor.prepare(json.dumps(facts,separators=(',',':'))):raise RuntimeError('Architecture authoring failed')
    unreal.EditorAssetLibrary.save_loaded_asset(layer);unreal.EditorAssetLibrary.save_loaded_asset(resources,only_if_is_dirty=False)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    unreal.log('heyzed prepared architecture: '+label+'; '+str(len(facts['cells']))+' native construction cells')


if __name__=='__main__':main()
