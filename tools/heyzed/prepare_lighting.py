"""Native Unreal resources and external actors for the prepared outdoor fixtures."""
import hashlib
import gc
import json
from pathlib import Path
import sys
import unreal

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import texture_policy
import material_usage

tools=unreal.AssetToolsHelpers.get_asset_tools()
library=unreal.MaterialEditingLibrary
source=json.loads((ROOT/'lighting/sources.json').read_text())
profile=json.loads((ROOT/'lighting/fixture.json').read_text())
for record in source['files']:
    if hashlib.sha256((ROOT/'lighting'/record['file']).read_bytes()).hexdigest()!=record['sha256']:
        raise RuntimeError('Fixture source differs: '+record['file'])
task=unreal.AssetImportTask()
for key,value in dict(filename=str(ROOT/'lighting/street_lamp_01.gltf'),destination_path='/Game/HeyZed/Lighting/StreetLamp',automated=True,save=True,replace_existing=True).items():task.set_editor_property(key,value)
tools.import_asset_tasks([task])
assets=[unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets('/Game/HeyZed/Lighting/StreetLamp',recursive=True,include_folder=False)]
mesh=next(a for a in assets if isinstance(a,unreal.StaticMesh))
for asset in assets:
    if isinstance(asset,unreal.Texture2D):
        normal=asset.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_NORMALMAP
        texture_policy.surface_texture(asset,'nor_dx' if normal else 'diff' if asset.get_editor_property('srgb') else 'rough')
        unreal.EditorAssetLibrary.save_loaded_asset(asset)

path='/Game/HeyZed/Lighting/M_Bulb'
bulb=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_Bulb','/Game/HeyZed/Lighting',unreal.Material,unreal.MaterialFactoryNew())
library.delete_all_material_expressions(bulb)
data=library.create_material_expression(bulb,unreal.MaterialExpressionPerInstanceCustomData)
data.set_editor_property('data_index',0)
color=library.create_material_expression(bulb,unreal.MaterialExpressionConstant3Vector);color.set_editor_property('constant',unreal.LinearColor(8,4,1))
multiply=library.create_material_expression(bulb,unreal.MaterialExpressionMultiply)
library.connect_material_expressions(data,'',multiply,'A');library.connect_material_expressions(color,'',multiply,'B')
library.connect_material_property(multiply,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
material_usage.require(bulb,nanite=True,instanced=True)
library.recompile_material(bulb)
slots=mesh.get_editor_property('static_materials')
bulb_slot=next(i for i,s in enumerate(slots) if 'bulb' in str(s.material_slot_name).lower())
mesh.set_material(bulb_slot,bulb)
non_shadow=[i for i,slot in enumerate(slots) if any(name in str(slot.material_slot_name).lower() for name in ('bulb','glass'))]
material_usage.fixture_mesh(mesh)
if not unreal.HeyZedLightResources.prepare_fixture_mesh(mesh,non_shadow):raise RuntimeError('Fixture mesh preparation failed')
unreal.EditorAssetLibrary.save_loaded_asset(mesh);unreal.EditorAssetLibrary.save_loaded_asset(bulb)
far=tools.create_asset('M_DistantStreetGlow','/Game/HeyZed/Lighting',unreal.Material,unreal.MaterialFactoryNew()) if not unreal.EditorAssetLibrary.does_asset_exist('/Game/HeyZed/Lighting/M_DistantStreetGlow') else unreal.load_asset('/Game/HeyZed/Lighting/M_DistantStreetGlow')
library.delete_all_material_expressions(far)
far.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
far.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
far_data=library.create_material_expression(far,unreal.MaterialExpressionPerInstanceCustomData);far_data.set_editor_property('data_index',0)
far_color=library.create_material_expression(far,unreal.MaterialExpressionConstant3Vector);far_color.set_editor_property('constant',unreal.LinearColor(40,20,5))
far_multiply=library.create_material_expression(far,unreal.MaterialExpressionMultiply)
library.connect_material_expressions(far_data,'',far_multiply,'A');library.connect_material_expressions(far_color,'',far_multiply,'B')
library.connect_material_property(far_multiply,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
library.connect_material_property(far_data,'',unreal.MaterialProperty.MP_OPACITY_MASK)
library.set_material_usage(far,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
library.recompile_material(far);unreal.EditorAssetLibrary.save_loaded_asset(far)

resource_path='/Game/HeyZed/Lighting/Resources'
if unreal.EditorAssetLibrary.does_asset_exist(resource_path):resources=unreal.load_asset(resource_path)
else:
    factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.HeyZedLightResources)
    resources=tools.create_asset('Resources','/Game/HeyZed/Lighting',unreal.HeyZedLightResources,factory)
resources.set_editor_property('street_lamp',mesh);resources.set_editor_property('bulb_material',bulb);resources.set_editor_property('bulb_slot',bulb_slot)
resources.set_editor_property('far_glow_material',far)
resources.set_editor_property('emitter',unreal.Vector(*profile['emitterCm']))
unreal.EditorAssetLibrary.save_loaded_asset(resources)

prepared=json.loads((ROOT/'.local/lighting/current.json').read_text())
world=json.loads((ROOT/'client/Saved/HeyZed/baked-world.json').read_text())
if prepared['identity']!=world['identity']:raise RuntimeError('Fixture and floor source identities differ')
unreal.EditorLoadingAndSavingUtils.load_map(world['world'])
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
descriptors=unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
if isinstance(descriptors,tuple) and isinstance(descriptors[0],bool):descriptors=descriptors[1]
done={str(desc.label) for desc in descriptors if str(desc.label).startswith('Lighting_')}
existing={a.get_actor_label():a for a in actors.get_all_level_actors() if isinstance(a,unreal.HeyZedLightRegion)}
for start in range(0,len(prepared['regions']),32):
    for region in prepared['regions'][start:start+32]:
        label=f'Lighting_{region["x"]}_{region["y"]}'
        if label in done:continue
        actor=existing.get(label) or actors.spawn_actor_from_class(unreal.HeyZedLightRegion,unreal.Vector(region['x']*25600,region['y']*25600,0))
        actor.set_actor_label(label);actor.set_editor_property('identity',prepared['identity'])
        fixtures=[];transforms=[]
        for item in region['fixtures']:
            fixture=unreal.HeyZedFixture()
            for key in ('id','radius','controls'):fixture.set_editor_property(key,item[key])
            fixture.set_editor_property('position',unreal.Vector(*item['position']))
            fixture.set_editor_property('color',unreal.LinearColor(*item['color']))
            fixtures.append(fixture)
            transforms.append(unreal.Transform(location=unreal.Vector(item['position'][0]-region['x']*25600,item['position'][1]-region['y']*25600,item['position'][2])))
        actor.set_editor_property('fixtures',fixtures)
        components=actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
        poles=next(c for c in components if c.get_name()=='StreetFixtures');poles.set_static_mesh(mesh);poles.set_num_custom_data_floats(1)
        poles.clear_instances();poles.add_instances(transforms,False,False)
        glows=next(c for c in components if c.get_name()=='DistantStreetGlow');glows.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'));glows.set_material(0,far)
        glows.set_num_custom_data_floats(1);glows.clear_instances()
        emitter=profile['emitterCm']
        glows.add_instances([unreal.Transform(location=unreal.Vector(t.translation.x+emitter[0],t.translation.y+emitter[1],t.translation.z+emitter[2]),scale=unreal.Vector(.12,.12,.12)) for t in transforms],False,False)
        done.add(label)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    unreal.log(f'heyzed lighting regions saved: {min(start+32,len(prepared["regions"]))}/{len(prepared["regions"])}')
    actor=None;poles=None;glows=None
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    gc.collect();unreal.SystemLibrary.collect_garbage()
    unreal.EditorLoadingAndSavingUtils.load_map(world['world'])
    existing={a.get_actor_label():a for a in actors.get_all_level_actors() if isinstance(a,unreal.HeyZedLightRegion)}
unreal.log('heyzed lighting resources and native placements prepared')
