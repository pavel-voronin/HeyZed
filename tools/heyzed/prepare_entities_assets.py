"""Publish shared native entity meshes/materials without touching world placement."""
import gc
import hashlib
import json
import os
from pathlib import Path
import unreal

ROOT=Path(__file__).resolve().parent
SOURCE=ROOT/'.local/entities/native'
BASE='/Game/HeyZed/Entities'
VEHICLE_BYTES=(ROOT/'entities/vehicle-material.ush').read_bytes()
tools=unreal.AssetToolsHelpers.get_asset_tools()
edit=unreal.MaterialEditingLibrary

def setprop(obj,name,value):
    if hasattr(obj,'get_path_name') and name not in ('inputs','additional_outputs'):
        obj.set_editor_property(name,value,notify_mode=unreal.PropertyAccessChangeNotifyMode.NEVER)
    else:obj.set_editor_property(name,value)


def connect(source,output,target,pin):
    if not edit.connect_material_expressions(source,output,target,pin):raise RuntimeError(f'Entity material connection failed: {output} -> {pin}')
    return True


def property_link(source,output,target):
    if not edit.connect_material_property(source,output,target):raise RuntimeError(f'Entity material property connection failed: {output} -> {target}')
    return True


def material(name,glass=False):
    path=BASE+'/Materials/'+name
    asset=unreal.load_asset(path)
    recipe='native-entity-surface-v3'
    if asset and unreal.EditorAssetLibrary.get_metadata_tag(asset,'NativeEntityRecipe')==recipe:return asset
    if asset:edit.delete_all_material_expressions(asset)
    else:asset=tools.create_asset(name,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    setprop(asset,'two_sided',True)
    setprop(asset,'blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT if glass else unreal.BlendMode.BLEND_MASKED)
    setprop(asset,'opacity_mask_clip_value',.01)
    sample=edit.create_material_expression(asset,unreal.MaterialExpressionTextureSampleParameter2D)
    setprop(sample,'parameter_name','NativeDiffuse')
    setprop(sample,'texture',unreal.load_asset(BASE+'/Textures/T_Empty') or unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    tint=edit.create_material_expression(asset,unreal.MaterialExpressionVectorParameter);setprop(tint,'parameter_name','Tint');setprop(tint,'default_value',unreal.LinearColor(1,1,1,1))
    multiply=edit.create_material_expression(asset,unreal.MaterialExpressionMultiply)
    connect(sample,'RGB',multiply,'A');connect(tint,'',multiply,'B');property_link(multiply,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough=edit.create_material_expression(asset,unreal.MaterialExpressionConstant);setprop(rough,'r',.1 if glass else .75);property_link(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    if glass:
        setprop(asset,'translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        alpha=edit.create_material_expression(asset,unreal.MaterialExpressionCustom);setprop(alpha,'code','return A < 0.5 ? A : 0.0;');setprop(alpha,'output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        item=unreal.CustomInput();setprop(item,'input_name','A');setprop(alpha,'inputs',[item]);connect(sample,'A',alpha,'A');property_link(alpha,'',unreal.MaterialProperty.MP_OPACITY)
    else:property_link(sample,'A',unreal.MaterialProperty.MP_OPACITY_MASK)
    edit.set_base_material_usage(asset,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    errors=edit.recompile_material(asset)
    if errors:raise RuntimeError(f'Entity material compilation failed: {errors}')
    unreal.EditorAssetLibrary.set_metadata_tag(asset,"NativeEntityRecipe",recipe)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset

def vehicle_material(name,glass=False):
    path=BASE+'/Materials/'+name
    asset=unreal.load_asset(path)
    recipe=hashlib.sha256(VEHICLE_BYTES+b'native-vehicle-graph-v3').hexdigest()
    if asset and unreal.EditorAssetLibrary.get_metadata_tag(asset,'NativeEntityRecipe')==recipe:return asset
    if asset:edit.delete_all_material_expressions(asset)
    else:asset=tools.create_asset(name,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    setprop(asset,'two_sided',True)
    setprop(asset,'blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT if glass else unreal.BlendMode.BLEND_MASKED)
    if glass:setprop(asset,'translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    raw=ROOT/'.local/entities/empty.png';raw.parent.mkdir(parents=True,exist_ok=True)
    import struct,zlib
    def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data)&0xffffffff)
    raw.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(5)))+chunk(b'IEND',b''))
    empty=unreal.load_asset(BASE+'/Textures/T_Empty')
    if not empty:
        task=unreal.AssetImportTask();setprop(task,'filename',str(raw));setprop(task,'destination_path',BASE+'/Textures');setprop(task,'destination_name','T_Empty');setprop(task,'automated',True);setprop(task,'save',True);tools.import_asset_tasks([task]);empty=unreal.load_asset(BASE+'/Textures/T_Empty')
    setprop(empty,'srgb',True);unreal.EditorAssetLibrary.save_loaded_asset(empty)
    empty_data=unreal.load_asset(BASE+'/Textures/T_EmptyData')
    if not empty_data:
        task=unreal.AssetImportTask();setprop(task,'filename',str(raw));setprop(task,'destination_path',BASE+'/Textures');setprop(task,'destination_name','T_EmptyData');setprop(task,'automated',True);setprop(task,'save',True);tools.import_asset_tasks([task]);empty_data=unreal.load_asset(BASE+'/Textures/T_EmptyData')
    setprop(empty_data,'srgb',False);unreal.EditorAssetLibrary.save_loaded_asset(empty_data)
    custom=edit.create_material_expression(asset,unreal.MaterialExpressionCustom)
    setprop(custom,'code',VEHICLE_BYTES.decode('utf-8-sig'))
    setprop(custom,'output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    glow=unreal.CustomOutput();setprop(glow,'output_name','Glow');setprop(glow,'output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3);setprop(custom,'additional_outputs',[glow])
    inputs=[];connections=[]
    def bind(name,node,output=''):
        item=unreal.CustomInput();setprop(item,'input_name',name);inputs.append(item);connections.append((node,output,name))
    for name in ['Paint','Mask','Rust','Lamps','BloodMask','Damage1','Blood','Damage2']:
        node=edit.create_material_expression(asset,unreal.MaterialExpressionTextureObjectParameter);setprop(node,'parameter_name',name);setprop(node,'texture',empty_data if name=='Mask' else empty)
        setprop(node,'sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if name=='Mask' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        bind(name,node)
    for name in ['PaintHSV','Tint']+[name+str(i) for name in ['Removed','Damage1Enabled','Damage2Enabled','BloodAmount','BloodEnabled','Lit'] for i in range(7)]:
        node=edit.create_material_expression(asset,unreal.MaterialExpressionVectorParameter);setprop(node,'parameter_name',name);setprop(node,'default_value',unreal.LinearColor(1,1,1,1) if name=='Tint' else unreal.LinearColor(0,0,0,0));bind(name,node,'RGBA')
    for name in ['RustAmount','HasMask','MultiUV','RandomPaint']:
        node=edit.create_material_expression(asset,unreal.MaterialExpressionScalarParameter);setprop(node,'parameter_name',name);bind(name,node)
    for index,name in enumerate(['UV','UV2']):
        node=edit.create_material_expression(asset,unreal.MaterialExpressionTextureCoordinate);setprop(node,'coordinate_index',index);bind(name,node)
    for name,typ in [('Normal',unreal.MaterialExpressionVertexNormalWS),('View',unreal.MaterialExpressionCameraVectorWS)]:bind(name,edit.create_material_expression(asset,typ))
    node=edit.create_material_expression(asset,unreal.MaterialExpressionConstant);setprop(node,'r',1 if glass else 0);bind('GlassPass',node)
    setprop(custom,'inputs',inputs)
    for node,output,name in connections:
        if not connect(node,output,custom,name):raise RuntimeError('Native vehicle material input: '+name)
    rgb=edit.create_material_expression(asset,unreal.MaterialExpressionComponentMask);setprop(rgb,'r',True);setprop(rgb,'g',True);setprop(rgb,'b',True)
    connect(custom,'',rgb,'');property_link(rgb,'',unreal.MaterialProperty.MP_BASE_COLOR)
    alpha=edit.create_material_expression(asset,unreal.MaterialExpressionComponentMask);setprop(alpha,'r',False);setprop(alpha,'g',False);setprop(alpha,'b',False);setprop(alpha,'a',True);connect(custom,'',alpha,'')
    property_link(alpha,'',unreal.MaterialProperty.MP_OPACITY if glass else unreal.MaterialProperty.MP_OPACITY_MASK)
    property_link(custom,'Glow',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough=edit.create_material_expression(asset,unreal.MaterialExpressionConstant);setprop(rough,'r',.07 if glass else .45);property_link(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.set_base_material_usage(asset,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    errors=edit.recompile_material(asset)
    if errors:raise RuntimeError(f'Native vehicle material compilation failed: {errors}')
    unreal.EditorAssetLibrary.set_metadata_tag(asset,"NativeEntityRecipe",recipe);unreal.EditorAssetLibrary.save_loaded_asset(asset);return asset

def main():
    native=json.loads((SOURCE/'catalog.json').read_text(encoding='utf-8'))
    resource=unreal.load_asset(BASE+'/Resources')
    if not resource:
        factory=unreal.DataAssetFactory();setprop(factory,'data_asset_class',unreal.HeyZedEntityResources)
        resource=tools.create_asset('Resources',BASE,unreal.HeyZedEntityResources,factory)
    setprop(resource,'vehicle_material',vehicle_material('M_NativeVehicle'))
    setprop(resource,'vehicle_glass_material',vehicle_material('M_NativeVehicleGlass',True))
    setprop(resource,'material',material('M_NativeEntity'))
    setprop(resource,'glass_material',material('M_NativeEntityGlass',True))
    models={}
    unique={v['id']:v for v in native['models'].values()}
    limit=int(os.environ.get('HEYZED_ENTITY_ASSET_LIMIT','0'));only=os.environ.get('HEYZED_ENTITY_ASSET_ONLY','')
    done={};count=0
    for model_id,record in unique.items():
        text=(SOURCE/record['file']).read_text(encoding='utf-8')
        if only and only not in text:continue
        recipe=b'native-skeletal-unreduced-v3\0' if record['skeletal'] else b'native-facing-v2\0'
        fingerprint=hashlib.sha256(recipe+text.encode('utf-8')).hexdigest()[:12]
        path=BASE+'/Meshes/'+('SK_' if record['skeletal'] else 'SM_')+model_id+'_'+fingerprint
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            asset=unreal.HeyZedEntityResources.prepare_native_model(text,path)
            if not asset:raise RuntimeError(f'Native entity resource publication failed: {record["file"]}')
            unreal.EditorAssetLibrary.save_loaded_asset(asset);asset=None
        done[model_id]=path+'.'+path.rsplit('/',1)[-1]
        count+=1
        if count%25==0:
            for key,value in native['models'].items():
                if value['id'] in done:models[key]=done[value['id']]
            
            if not resource.register_native_models(json.dumps(models)):raise RuntimeError('Native entity resource bindings rejected')
            unreal.EditorAssetLibrary.save_loaded_asset(resource)
            gc.collect();unreal.SystemLibrary.collect_garbage();unreal.log(f'heyzed shared entity assets: {count} / {len(unique)}')
        if limit and count>=limit:break
    for key,value in native['models'].items():
        if value['id'] in done:models[key]=done[value['id']]
    if not resource.register_native_models(json.dumps(models)):raise RuntimeError('Native entity resource bindings rejected')
    if not limit and not only:
        (ROOT/'client/Content/HeyZed/Entities/Player').mkdir(parents=True,exist_ok=True)
        resource.set_editor_property('player_retargeters',{})
    unreal.EditorAssetLibrary.save_loaded_asset(resource)
    bindings=resource.native_model_count()
    folder=ROOT/'client/Saved/HeyZed';folder.mkdir(parents=True,exist_ok=True)
    (folder/('entities-assets-partial.json' if limit or only else 'entities-assets.json')).write_text(json.dumps(dict(schema=1,mesh_recipe='native-facing-v2-skeletal-unreduced-v3',models=count,bindings=bindings,complete=not limit and not only,native_errors=native['errors'])),encoding='utf-8')
    (folder/'entities-materials.json').write_text(json.dumps(dict(schema=1,shader_sha256=hashlib.sha256(VEHICLE_BYTES).hexdigest(),surface_recipe='native-entity-surface-v3',compiled='-nullrhi' not in unreal.SystemLibrary.get_command_line().lower())),encoding='utf-8')
    unreal.log(f'heyzed entity resource publication complete: {count} meshes, {bindings} bindings')


def publish():
    import time
    lock_path=ROOT/'.local/entities/author.lock'
    lock_path.parent.mkdir(parents=True,exist_ok=True)
    with lock_path.open('a+b') as lock:
        if os.name=='nt':
            import msvcrt
            while True:
                try:
                    lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1);break
                except OSError:time.sleep(1)
            try:main()
            finally:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
        else:
            import fcntl
            fcntl.flock(lock,fcntl.LOCK_EX)
            try:main()
            finally:fcntl.flock(lock,fcntl.LOCK_UN)

publish()
