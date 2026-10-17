"""Author reusable furniture models; positions come exclusively from loaded PZ chunks."""
import hashlib
import json
import os
from pathlib import Path
import sys
import unreal

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import material_usage
import texture_policy

BASE = '/Game/HeyZed/Furniture'
tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Furniture save failed: ' + asset.get_path_name())


def texture(path):
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    name = 'T_' + digest
    asset = unreal.load_asset(BASE+'/Textures/'+name)
    if asset is None:
        task = unreal.AssetImportTask()
        for key, value in dict(filename=path, destination_path=BASE+'/Textures', destination_name=name,
                               automated=True, save=True, replace_existing=False).items():
            task.set_editor_property(key, value)
        tools.import_asset_tasks([task]); asset = unreal.load_asset(BASE+'/Textures/'+name)
        if asset is None: raise RuntimeError('Furniture texture import failed: ' + path)
        asset.set_editor_property('power_of_two_mode', unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
        asset.set_editor_property('do_scale_mips_for_alpha_coverage', True)
        asset.set_editor_property('alpha_coverage_thresholds', unreal.Vector4(0,0,0,.5))
        texture_policy.surface_texture(asset, 'diff'); save(asset)
    return asset


def material(spec):
    # Texture alpha is an authored cutout, not an automatic translucent material.
    spec = dict(spec)
    masked = spec['masked']
    if spec.get('texture'):
        image = Path(spec['texture'])
        spec['textureSha256'] = hashlib.sha256(image.read_bytes()).hexdigest()
    identity = {k: v for k, v in spec.items() if k != 'texture'}
    recipe = hashlib.sha256((json.dumps(identity, sort_keys=True)+'furniture-material-1').encode()).hexdigest()
    path = BASE+'/Materials/MI_'+recipe
    asset = unreal.load_asset(path)
    if asset: return asset
    tex = texture(spec['texture']) if spec.get('texture') else unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
    parent_name = 'M_PackMasked' if masked else 'M_PackOpaque'
    parent = unreal.load_asset(BASE+'/Materials/'+parent_name)
    if parent is None:
        parent = tools.create_asset(parent_name, BASE+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
        def node(cls): return edit.create_material_expression(parent, cls)
        def scalar(name, value):
            n = node(unreal.MaterialExpressionScalarParameter); n.set_editor_property('parameter_name', name); n.set_editor_property('default_value', value); return n
        color = node(unreal.MaterialExpressionVectorParameter)
        color.set_editor_property('parameter_name', 'Tint'); color.set_editor_property('default_value', unreal.LinearColor(1,1,1,1))
        sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
        sample.set_editor_property('parameter_name', 'Diffuse'); sample.set_editor_property('texture', tex)
        sample.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        product = node(unreal.MaterialExpressionMultiply)
        edit.connect_material_expressions(sample, 'RGB', product, 'A')
        edit.connect_material_expressions(color, '', product, 'B')
        edit.connect_material_property(product, '', unreal.MaterialProperty.MP_BASE_COLOR)
        if masked:
            opacity = node(unreal.MaterialExpressionMultiply)
            edit.connect_material_expressions(sample, 'A', opacity, 'A')
            edit.connect_material_expressions(scalar('Opacity', 1), '', opacity, 'B')
            edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY_MASK)
        if masked:
            parent.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
            parent.set_editor_property('opacity_mask_clip_value', .5)
        edit.connect_material_property(scalar('Roughness', .65), '', unreal.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(scalar('Metallic', 0), '', unreal.MaterialProperty.MP_METALLIC)
        material_usage.require(parent, nanite=True, instanced=True)
        edit.recompile_material(parent); save(parent)
    asset = tools.create_asset('MI_'+recipe, BASE+'/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(asset, parent)
    edit.set_material_instance_texture_parameter_value(asset, 'Diffuse', tex)
    edit.set_material_instance_vector_parameter_value(asset, 'Tint', unreal.LinearColor(*spec['color']))
    for name, value in [('Roughness', spec['roughness']), ('Metallic', spec['metallic']), ('Opacity', spec['opacity'])]:
        edit.set_material_instance_scalar_parameter_value(asset, name, value)
    material_usage.require(asset, nanite=True, instanced=True)
    edit.update_material_instance(asset); save(asset)
    return asset


def main():
    source = Path(os.environ.get('HEYZED_FURNITURE_PREPARED', str(ROOT/'.local/furniture-catalog/prepared.json'))).resolve()
    data = json.loads(source.read_text())
    if data['schema'] != 3: raise ValueError('Resource-only furniture catalogue required')
    path = BASE+'/Library_world'
    library = unreal.load_asset(path)
    if library is None:
        factory = unreal.DataAssetFactory(); factory.set_editor_property('data_asset_class', unreal.HeyZedFurnitureLibrary)
        library = tools.create_asset('Library_world', BASE, unreal.HeyZedFurnitureLibrary, factory)
    library.set_editor_property('meshes', {})
    library.set_editor_property('binding_description', json.dumps(data['bindings'], separators=(',', ':')))
    for index, model in enumerate(data['models']):
        mesh_path = BASE+'/Meshes/SM_'+model['key']
        mesh = unreal.load_asset(mesh_path)
        recipe = hashlib.sha256((model['recipe']+Path(__file__).read_text()+(ROOT/'client/Source/HeyZed/HeyZedFurnitureAuthoring.cpp').read_text()).encode()).hexdigest()
        if mesh is None or unreal.EditorAssetLibrary.get_metadata_tag(mesh, 'FurnitureRecipe') != recipe:
            mats = [material(m) for m in model['materials']]
            mesh_input = Path(model['meshInput'])
            if not mesh_input.is_absolute(): mesh_input = ROOT.parent.parent/mesh_input
            if not library.prepare_mesh(model['key'], str(mesh_input.resolve()), mats):
                raise RuntimeError('Furniture mesh authoring failed: ' + model['key'])
            mesh = unreal.load_asset(mesh_path)
            unreal.EditorAssetLibrary.set_metadata_tag(mesh, 'FurnitureRecipe', recipe); save(mesh)
        # UPROPERTY TSoftObjectPtr owns only the path; the cooked actor does not retain the full pack.
        library.register_mesh(model['key'], mesh_path+'.'+mesh_path.rsplit('/',1)[1])
        if (index+1)%16 == 0:
            save(library); del mesh; unreal.SystemLibrary.collect_garbage()
        unreal.log('heyzed furniture resource '+str(index+1)+'/'+str(len(data['models'])))
    save(library)
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = json.loads((ROOT/'client/Saved/HeyZed/baked-world.json').read_text())['world']
    if not world.startswith('/Game/HeyZed/Baked/'): raise RuntimeError('Prepared World Partition map required')
    if not level.load_level(world): raise RuntimeError('Furniture world load failed')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    if isinstance(descs, tuple) and isinstance(descs[0], bool): descs = descs[1]
    legacy = [d.guid for d in descs if str(d.label).startswith(('Furniture_', 'FurnitureWorld_'))]
    if legacy: unreal.WorldPartitionBlueprintLibrary.load_actors(legacy)
    removed = 0
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.HeyZedFurnitureRegion):
            actors.destroy_actor(actor); removed += 1
    if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True): raise RuntimeError('Furniture world migration save failed')
    receipt = ROOT/'client/Saved/HeyZed/furniture.json'; receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(dict(schema=3, publication=os.environ.get('HEYZED_FURNITURE_PUBLICATION',''),
        models=len(data['models']), tiles=len(data['bindings']), removedBakedActors=removed,
        packSha256=data['packSha256'], source=str(source), mode='native_chunks',
        validation='Resource authoring only; runtime and visual acceptance pending.')), encoding='utf-8')
    unreal.log('heyzed furniture resource authoring complete')


if __name__ == '__main__': main()
