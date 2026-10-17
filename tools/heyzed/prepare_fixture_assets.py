"""Author free fixture models, materials and laboratory bindings in place."""
import hashlib
import json
from pathlib import Path
import sys
import unreal
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import texture_policy
import material_usage
from prepare_fixture_catalog import prepare
entries=prepare()
tools=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary
BASE='/Game/HeyZed/Lighting'
def material(name,color,emission=False):
    path=BASE+'/'+name
    m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name,BASE,unreal.Material,unreal.MaterialFactoryNew())
    material_usage.require(m,nanite=True,instanced=True)
    stamp=hashlib.sha256(repr((color,emission,1)).encode()).hexdigest()
    if unreal.EditorAssetLibrary.get_metadata_tag(m,'FixtureRecipe')==stamp:return m
    edit.delete_all_material_expressions(m)
    c=edit.create_material_expression(m,unreal.MaterialExpressionConstant3Vector);c.set_editor_property('constant',unreal.LinearColor(*color))
    edit.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
    r=edit.create_material_expression(m,unreal.MaterialExpressionConstant);r.set_editor_property('r',.65)
    edit.connect_material_property(r,'',unreal.MaterialProperty.MP_ROUGHNESS)
    if emission:
        e=edit.create_material_expression(m,unreal.MaterialExpressionVectorParameter);e.set_editor_property('parameter_name','Emission');e.set_editor_property('default_value',unreal.LinearColor(0,0,0))
        edit.connect_material_property(e,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(m);unreal.EditorAssetLibrary.set_metadata_tag(m,'FixtureRecipe',stamp);unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m
emission=material('M_FixtureEmission',(.7,.7,.7),True)
housing=material('M_FixtureHousing',(.025,.035,.03))
support=material('M_FixtureSupport',(.19,.11,.055))
models={}
for name in ['desk_lamp_arm_01','pull_chain_light_socket','vintage_flashlight']:
    folder=ROOT/'lighting/fixtures'/name
    source=json.loads((folder/'source.json').read_text(encoding='utf-8'))
    for record in source['files']:
        if hashlib.sha256((folder/record['file']).read_bytes()).hexdigest()!=record['sha256']:raise ValueError('Fixture source checksum mismatch')
    destination=BASE+'/Fixtures/'+name
    available=[unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(destination,True,False)] if unreal.EditorAssetLibrary.does_directory_exist(destination) else []
    mesh=next((a for a in available if isinstance(a,unreal.StaticMesh)),None)
    stamp=hashlib.sha256((folder/'source.json').read_bytes()).hexdigest()
    if mesh is None or unreal.EditorAssetLibrary.get_metadata_tag(mesh,'FixtureSource')!=stamp:
        task=unreal.AssetImportTask()
        for key,value in dict(filename=str(folder/(name+'.gltf')),destination_path=destination,automated=True,save=True,replace_existing=True).items():task.set_editor_property(key,value)
        tools.import_asset_tasks([task])
        available=[unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(destination,True,False)]
        mesh=next(a for a in available if isinstance(a,unreal.StaticMesh))
        unreal.EditorAssetLibrary.set_metadata_tag(mesh,'FixtureSource',stamp)
    non_shadow=[]
    for i,slot in enumerate(mesh.get_editor_property('static_materials')):
        label=str(slot.material_slot_name).lower()
        if slot.material_interface is not None:label+=' '+slot.material_interface.get_name().lower()
        if any(part.endswith(('_light','_glass','_bulb')) for part in label.split()):
            mesh.set_material(i,emission);non_shadow.append(i)
    if name in ('desk_lamp_arm_01','vintage_flashlight') and not non_shadow:raise RuntimeError('Missing authored lens material: '+name)
    material_usage.fixture_mesh(mesh)
    unreal.HeyZedLightResources.prepare_fixture_mesh(mesh,non_shadow)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    for asset in available:
        if isinstance(asset,unreal.Texture2D):
            normal=asset.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_NORMALMAP
            texture_policy.surface_texture(asset,'nor_dx' if normal else 'diff' if asset.get_editor_property('srgb') else 'rough')
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
    models[name]=mesh
street_path=BASE+'/Fixtures/SM_StreetPreview'
if unreal.EditorAssetLibrary.does_asset_exist(street_path):street=unreal.load_asset(street_path)
else:
    resources=unreal.load_asset(BASE+'/Resources')
    street=unreal.EditorAssetLibrary.duplicate_asset(resources.get_editor_property('street_lamp').get_path_name(),street_path)
for i,slot in enumerate(street.get_editor_property('static_materials')):
    if any(token in str(slot.material_slot_name).lower() for token in ['glass','bulb']):street.set_material(i,emission)
material_usage.fixture_mesh(street)
unreal.EditorAssetLibrary.save_loaded_asset(street);models['street_lamp_01']=street
models['native_switch_plate']=unreal.load_asset('/Engine/BasicShapes/Cube')
path=BASE+'/FixtureLibrary'
if unreal.EditorAssetLibrary.does_asset_exist(path):resource=unreal.load_asset(path)
else:
    factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.HeyZedFixtureLibrary)
    resource=tools.create_asset('FixtureLibrary',BASE,unreal.HeyZedFixtureLibrary,factory)
profiles={}
source=json.loads((ROOT/'lighting/fixture-profiles.json').read_text(encoding='utf-8-sig'))
for kind,values in source['profiles'].items():
    profile=unreal.HeyZedFixtureProfile()
    if values.get('asset'):profile.set_editor_property('mesh',models[values['asset']])
    for field,key in [('mesh_offset','meshOffsetCm'),('mesh_scale','meshScale'),('emitter','emitterCm'),('direction','direction')]:
        if key in values:profile.set_editor_property(field,unreal.Vector(*values[key]))
    if 'meshRotationDegrees' in values:profile.set_editor_property('mesh_rotation',unreal.Rotator(*values['meshRotationDegrees']))
    for field,key in [('lumens','lumens'),('outer_angle','outerAngle'),('bulb','bulb'),('sign','sign')]:
        if key in values:profile.set_editor_property(field,values[key])
    profiles[kind]=profile
resource.set_editor_property('profiles',profiles)
resource.set_editor_property('cube',unreal.load_asset('/Engine/BasicShapes/Cube'))
resource.set_editor_property('sphere',unreal.load_asset('/Engine/BasicShapes/Sphere'))
# The native source catalogue is authoritative; these are representative models.
resource.set_editor_property('bindings',{e['tile'].replace('_on_','_'):e['profile'] for e in entries if e['profile']})
resource.set_editor_property('emission_material',emission);resource.set_editor_property('housing_material',housing);resource.set_editor_property('support_material',support)
unreal.EditorAssetLibrary.save_loaded_asset(resource)
unreal.log(f'heyzed fixture library complete: {len(entries)} source records, {len(profiles)} authored profiles')
