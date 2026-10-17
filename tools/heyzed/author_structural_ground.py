"""Author one experimental structural-ground sample with shared repeating materials."""
import hashlib
import json
from pathlib import Path
import time
import unreal

ROOT = Path(__file__).resolve().parent
COMMON = '/Game/HeyZed/GroundRegions/Common'
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDIT = unreal.MaterialEditingLibrary


def create(name, folder, kind, factory):
    asset = unreal.load_asset(folder+'/'+name) if unreal.EditorAssetLibrary.does_asset_exist(folder+'/'+name) else None
    return asset or TOOLS.create_asset(name, folder, kind, factory)


def shared_master(default):
    code = (ROOT/'region-surface.ush').read_text('utf-8')
    revision = hashlib.sha256(code.encode()+b'regional-material-graph-1').hexdigest()[:12]
    path = COMMON+'/M_RegionSurface_'+revision
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset:
        return asset
    asset = TOOLS.create_asset('M_RegionSurface_'+revision, COMMON, unreal.Material, unreal.MaterialFactoryNew())
    def node(kind):
        return EDIT.create_material_expression(asset, kind)
    inputs = {}
    for index in range(4):
        for key, field, sampler in [('Diffuse','diffuse',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),('Normal','normal',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),('Rough','roughness',unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)]:
            name = key+str(index)
            value = node(unreal.MaterialExpressionTextureObjectParameter)
            value.set_editor_property('parameter_name', name)
            value.set_editor_property('texture', default.get_editor_property(field))
            value.set_editor_property('sampler_type', sampler)
            inputs[name] = value
        for key, value in [('Width',100),('Road',0),('Aggregate',0),('Paint',0)]:
            expression = node(unreal.MaterialExpressionScalarParameter)
            expression.set_editor_property('parameter_name', key+str(index))
            expression.set_editor_property('default_value', value)
            inputs[key+str(index)] = expression
        tint = node(unreal.MaterialExpressionVectorParameter)
        tint.set_editor_property('parameter_name', 'Tint'+str(index))
        tint.set_editor_property('default_value', unreal.LinearColor(1,1,1,1))
        inputs['Tint'+str(index)] = tint
    inputs['World'] = node(unreal.MaterialExpressionWorldPosition)
    inputs['Gates'] = node(unreal.MaterialExpressionVertexColor)
    for name in ('Origin', 'Region'):
        expression = node(unreal.MaterialExpressionVectorParameter)
        expression.set_editor_property('parameter_name', 'SurfaceOrigin' if name == 'Origin' else 'BakedRegion')
        inputs[name] = expression
    for name, value in [('LayerCount',1),('MaskEnabled',0)]:
        expression = node(unreal.MaterialExpressionScalarParameter)
        expression.set_editor_property('parameter_name', name)
        expression.set_editor_property('default_value', value)
        inputs[name] = expression
    mask = node(unreal.MaterialExpressionTextureObjectParameter)
    mask.set_editor_property('parameter_name', 'BakedMask')
    mask.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    mask.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    inputs['MaskTexture'] = mask
    custom = node(unreal.MaterialExpressionCustom)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput();pin.set_editor_property('input_name', name);pins.append(pin)
    custom.set_editor_property('inputs', pins)
    outputs = []
    for name, kind in [('Normal',unreal.CustomMaterialOutputType.CMOT_FLOAT3),('Roughness',unreal.CustomMaterialOutputType.CMOT_FLOAT1),('Visibility',unreal.CustomMaterialOutputType.CMOT_FLOAT1)]:
        pin = unreal.CustomOutput();pin.set_editor_property('output_name', name);pin.set_editor_property('output_type', kind);outputs.append(pin)
    custom.set_editor_property('additional_outputs', outputs)
    custom.set_editor_property('code', code)
    for name, expression in inputs.items():
        if not EDIT.connect_material_expressions(expression, '', custom, name):
            raise RuntimeError('Regional material input connection failed: '+name)
    for output, prop in [('',unreal.MaterialProperty.MP_BASE_COLOR),('Normal',unreal.MaterialProperty.MP_NORMAL),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS),('Visibility',unreal.MaterialProperty.MP_OPACITY_MASK)]:
        if not EDIT.connect_material_property(custom, output, prop):
            raise RuntimeError('Regional material output connection failed: '+output)
    asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    EDIT.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Regional master material save failed')
    return asset


def main():
    started = time.monotonic()
    manifest = json.loads((ROOT/'.local/ground-structure/current.json').read_text('utf-8'))
    path = Path(manifest['file'])
    data = json.loads(path.read_text('utf-8'))
    if data['representation'] != 'structural-ground-regions' or data['region'] != [31,45]:
        raise RuntimeError('Only the agreed single region 31_45 may be authored')
    ground = unreal.load_asset('/Game/HeyZed/GroundLibrary')
    if not ground:
        raise RuntimeError('Shared ground material library missing')
    styles = {str(k): v for k, v in ground.get_editor_property('surfaces').items()}
    master = shared_master(styles['Concrete'])
    revision = hashlib.sha256(path.read_bytes()+(ROOT/'region-surface.ush').read_bytes()).hexdigest()[:16]
    name = 'Region_31_45_'+revision
    folder = '/Game/HeyZed/GroundRegions/Samples'
    package = folder+'/'+name
    programs = data['surface_programs']
    names = sorted(programs)
    variants = []
    for level in range(3):
        for key in names:
            materials = programs[key]
            instance = create('MI_'+master.get_name()+'_'+str(level)+'_'+key, COMMON, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            EDIT.set_material_instance_parent(instance, master)
            EDIT.set_material_instance_scalar_parameter_value(instance, 'LayerCount', len(materials))
            EDIT.set_material_instance_vector_parameter_value(instance, 'SurfaceOrigin', unreal.LinearColor(level*30000,0,0,0))
            for index in range(4):
                material = materials[min(index,len(materials)-1)]
                known = material in styles
                style = styles.get(material, styles['Concrete'])
                for parameter, field in [('Diffuse','diffuse'),('Normal','normal'),('Rough','roughness')]:
                    EDIT.set_material_instance_texture_parameter_value(instance, parameter+str(index), style.get_editor_property(field))
                tint = style.get_editor_property('tint') if known else unreal.LinearColor(1,0,1,1)
                EDIT.set_material_instance_vector_parameter_value(instance, 'Tint'+str(index), tint)
                for parameter, value in [('Width',style.get_editor_property('width')*100),('Road',float(material.startswith('Road_') and material!='Road_04')),('Aggregate',float(material in ('Road_04','Gravel'))),('Paint',float(material.startswith('Paint')))]:
                    EDIT.set_material_instance_scalar_parameter_value(instance, parameter+str(index), value)
            EDIT.update_material_instance(instance)
            unreal.EditorAssetLibrary.save_loaded_asset(instance)
            variants.append(instance)
    report_path = path.parent/'native-result.json'
    if not unreal.HeyZedGroundRegionAuthor.build(str(path), package, names, variants, str(report_path)):
        raise RuntimeError('Native structural region authoring failed')
    report = json.loads(report_path.read_text('utf-8-sig'))
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    origin = unreal.Vector(data['region'][0]*25600, data['region'][1]*25600, 0)
    for index, level in enumerate(report['levels']):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, origin+unreal.Vector(-30000*index,0,0))
        actor.set_actor_label('Structural '+level['name'])
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        component.set_static_mesh(unreal.load_asset(level['mesh']))
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_cast_shadow(False)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase.static_class())
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(), unreal.Rotator(pitch=-45,yaw=-35)).get_component_by_class(unreal.DirectionalLightComponent)
    sun.set_mobility(unreal.ComponentMobility.MOVABLE);sun.set_intensity(100000);sun.set_editor_property('atmosphere_sun_light',True)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector())
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector()).get_component_by_class(unreal.SkyLightComponent)
    sky.set_mobility(unreal.ComponentMobility.MOVABLE);sky.set_editor_property('real_time_capture',True)
    post = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector());post.set_editor_property('unbound',True)
    settings = post.get_editor_property('settings')
    for key, value in [('override_auto_exposure_min_brightness',True),('override_auto_exposure_max_brightness',True),('auto_exposure_min_brightness',15),('auto_exposure_max_brightness',15)]:
        settings.set_editor_property(key,value)
    post.set_editor_property('settings',settings)
    unreal.HeyZedGroundRegionAuthor.configure_preview(world,origin+unreal.Vector(-17200,-10000,48000),unreal.Rotator(pitch=-65,yaw=90))
    preview = package+'_Preview'
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, preview):
        raise RuntimeError('Separate structural-ground preview save failed')
    report.update(preview_complete=True, preview_world=preview, author_seconds=time.monotonic()-started,
                  structural_bytes=manifest['structural_bytes'], label_grid_bytes=manifest['label_grid_bytes'],
                  unresolved_material_counts=data['unresolved_material_counts'], unresolved_profile_counts=data['unresolved_profile_counts'],
                  geometry_overlap_policy='One planar subdivision; all surface vertices at z=0; material transitions use vertex gates')
    report_path.write_text(json.dumps(report,indent=2),'utf-8')
    manifest.update(native_report=str(report_path), preview_world=preview)
    (ROOT/'.local/ground-structure/current.json').write_text(json.dumps(manifest,indent=2),'utf-8')
    unreal.log('heyzed structural-ground single-region sample saved; runtime integration and visual acceptance pending')


if __name__ == '__main__':
    main()
