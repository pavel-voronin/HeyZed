"""Publish native crowd resources without changing world placement or nearby actors."""
import copy
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / '.local/entities/crowd'
BASE = '/Game/HeyZed/Entities'
CLIPS = {'idle':'Zombie_Idle', 'walk':'Zombie_Walk', 'run':'Zombie_Sprint', 'attack':'Zombie_DoorBang', 'sit':'Zombie_IdleAgainstWall', 'climb':'Zombie_ClimbWindowStart', 'crawl':'Zombie_Crawl', 'crawl_idle':'Zombie_CrawlIdle'}

def main():
    source = json.loads((SOURCE/'source.json').read_text(encoding='utf-8'))
    model_path = BASE+'/Meshes/Crowd_'+source['digest']
    mesh = unreal.HeyZedEntityResources.prepare_native_model((SOURCE/'body.json').read_text(encoding='utf-8'), model_path)
    if not mesh:
        raise RuntimeError('Native crowd mesh authoring failed')
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    lods = [subsystem.get_num_verts(mesh, index) for index in range(3)]
    sections = [subsystem.get_num_sections(mesh, index) for index in range(3)]
    if not (lods[0] >= lods[1] > lods[2] > 0 and sections == [1,1,1]):
        raise RuntimeError('Crowd LOD/section contract failed: '+str((lods,sections)))
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    edit = unreal.MaterialEditingLibrary
    materials = []
    for index in range(4):
        texture_path = BASE+'/Crowd/T_'+source['digest']+'_'+str(index)
        texture = unreal.load_asset(texture_path)
        if not texture:
            task = unreal.AssetImportTask();task.filename=str(SOURCE/f'outfit-{index}.png');task.destination_path=BASE+'/Crowd';task.destination_name=texture_path.rsplit('/',1)[1];task.automated=True;task.save=True
            tools.import_asset_tasks([task]);texture=unreal.load_asset(texture_path)
        if not texture:raise RuntimeError('Crowd texture import failed')
        texture.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        texture.set_editor_property('never_stream',False)
        path=BASE+'/Crowd/M_'+source['digest']+'_'+str(index)
        material=unreal.load_asset(path)
        if not material:
            material=tools.create_asset(path.rsplit('/',1)[1],BASE+'/Crowd',unreal.Material,unreal.MaterialFactoryNew())
            sample=edit.create_material_expression(material,unreal.MaterialExpressionTextureSample);sample.set_editor_property('texture',texture)
            if not edit.connect_material_property(sample,'RGB',unreal.MaterialProperty.MP_BASE_COLOR):raise RuntimeError('Crowd material wiring failed')
            rough=edit.create_material_expression(material,unreal.MaterialExpressionConstant);rough.set_editor_property('r',.85)
            edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
            edit.set_base_material_usage(material,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH);edit.recompile_material(material)
        unreal.EditorAssetLibrary.save_loaded_asset(texture,only_if_is_dirty=False);unreal.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False);materials.append(material)
    available={}
    for folder in [ROOT/'.local/entities/locomotion',SOURCE/'animations']:
        catalog=folder/'catalog.json'
        if not catalog.exists():continue
        records=json.loads(catalog.read_text(encoding='utf-8'))['sources']
        wanted={name.lower() for name in CLIPS.values()}
        for source_key,record in records.items():
            if Path(source_key).stem.lower() not in wanted:continue
            file=folder/record['file']
            data=json.loads(file.read_text(encoding='utf-8'))
            for clip in data['clips']:
                if clip['name'].lower() in {name.lower() for name in CLIPS.values()}:available[clip['name'].lower()]=(data,clip)
    animations={}
    for state,name in CLIPS.items():
        if name.lower() not in available:raise RuntimeError('Required native crowd clip missing: '+name)
        data,clip=available[name.lower()];clip=copy.deepcopy(clip)
        # Native presentation cancels the deferred horizontal translation on this unweighted data bone.
        for key in clip['tracks'].get('Translation_Data',[]):
            key['matrix'][12]=0;key['matrix'][14]*=2
        data=dict(data,clips=[clip]);text=json.dumps(data,separators=(',',':'))
        digest=hashlib.sha256(('crowd-in-place-v1'+model_path+text).encode()).hexdigest()[:20]
        path=BASE+'/Animations/Crowd/A_'+digest
        animation=unreal.HeyZedEntityResources.prepare_native_animation(text,clip['name'],mesh,path)
        if not animation:raise RuntimeError('Crowd animation authoring failed: '+name)
        animations[state]=animation
    animations['walk_slow']=animations['walk']
    resources=unreal.load_asset(BASE+'/Resources')
    resources.set_editor_property('crowd_mesh',mesh);resources.set_editor_property('crowd_scale',1.7/source['height'])
    resources.set_editor_property('crowd_materials',materials);resources.set_editor_property('crowd_animations',animations)
    if not unreal.EditorAssetLibrary.save_loaded_asset(resources,only_if_is_dirty=False):raise RuntimeError('Crowd resource publication failed')
    result=dict(recipe=source['recipe'],mesh=mesh.get_path_name(),lod_vertices=lods,lod_sections=sections,animations={key:value.get_path_name() for key,value in animations.items()},textures=4,texture_size=256)
    output=ROOT/'client/Saved/HeyZed/crowd-assets.json';output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2),encoding='utf-8')
    unreal.log('heyzed crowd resources: '+json.dumps(result))

main()
