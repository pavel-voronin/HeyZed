"""Unreal Editor authoring only. The C++ game loads ordinary saved Unreal assets."""
from pathlib import Path
import hashlib
import json
import unreal
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import texture_policy
import prepare_lawn

ROOT = Path(__file__).resolve().parent
DESTINATION = '/Game/HeyZed'
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary


def texture(record, kind):
    source = ROOT/'materials'/record['file']
    if hashlib.sha256(source.read_bytes()).hexdigest() != record['sha256']:
        raise RuntimeError('Material source checksum differs: '+str(source))
    name = source.stem
    path = DESTINATION+'/Textures/'+name
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(source))
    task.set_editor_property('destination_path', DESTINATION+'/Textures')
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    tools.import_asset_tasks([task])
    result = unreal.load_asset(path)
    texture_policy.surface_texture(result, kind)
    unreal.EditorAssetLibrary.save_loaded_asset(result)
    return result


def custom_input(name):
    value=unreal.CustomInput();value.set_editor_property('input_name',name);return value


def custom_output(name,kind):
    value=unreal.CustomOutput();value.set_editor_property('output_name',name);value.set_editor_property('output_type',kind);return value


def material(name, maps, width, tint):
    path = DESTINATION+'/Materials/M_'+name
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_'+name, DESTINATION+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
    library.delete_all_material_expressions(asset)
    def node(cls): return library.create_material_expression(asset, cls)
    def link(a, output, b, pin):
        if not library.connect_material_expressions(a, output, b, pin): raise RuntimeError('Material connection failed: '+pin)
    inputs = {}
    for prefix in ('Base', 'Overlay'):
        for kind, label in [('diff','Diffuse'),('nor_dx','Normal'),('rough','Roughness'),('height','Height')]:
            value=node(unreal.MaterialExpressionTextureObjectParameter)
            value.set_editor_property('parameter_name',prefix+label);value.set_editor_property('texture',maps.get(kind,unreal.load_asset('/Engine/EngineResources/Gray')))
            value.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='nor_dx' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if kind in ('rough','height') else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
            inputs[prefix+label]=value
        value=node(unreal.MaterialExpressionVectorParameter);value.set_editor_property('parameter_name',prefix+'Tint');value.set_editor_property('default_value',unreal.LinearColor(*tint,1));inputs[prefix+'Tint']=value
        value=node(unreal.MaterialExpressionScalarParameter);value.set_editor_property('parameter_name',prefix+'Width');value.set_editor_property('default_value',width*100);inputs[prefix+'Width']=value
    soft=node(unreal.MaterialExpressionScalarParameter);soft.set_editor_property('parameter_name','GroundSoftBlend');soft.set_editor_property('default_value',0);inputs['GroundSoftBlend']=soft
    coat=node(unreal.MaterialExpressionScalarParameter);coat.set_editor_property('parameter_name','PaintCoat');coat.set_editor_property('default_value',float(name.startswith('Paint')));inputs['PaintCoat']=coat
    visible=node(unreal.MaterialExpressionScalarParameter);visible.set_editor_property('parameter_name','SurfaceVisible');visible.set_editor_property('default_value',float(name!='PaintHidden'))
    for parameter in ('BaseRoad','OverlayRoad'):
        value=node(unreal.MaterialExpressionScalarParameter);value.set_editor_property('parameter_name',parameter);value.set_editor_property('default_value',float(name.startswith('Road_') and name!='Road_04'));inputs[parameter]=value
    neutral=node(unreal.MaterialExpressionScalarParameter);neutral.set_editor_property('parameter_name','NeutralAggregate');neutral.set_editor_property('default_value',float(name in ('Road_04','Gravel')));inputs['NeutralAggregate']=neutral
    origin=node(unreal.MaterialExpressionVectorParameter);origin.set_editor_property('parameter_name','SurfaceOrigin');inputs['Origin']=origin
    wear=node(unreal.MaterialExpressionCustom);wear.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    wear.set_editor_property('inputs',[custom_input(key) for key in ['World','Origin','Coat']])
    wear.set_editor_property('code', 'if(Coat<.5)return 1; struct W { float h(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);} float n(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(h(i),h(i+float2(1,0)),f.x),lerp(h(i+float2(0,1)),h(i+1),f.x),f.y);}}; W f;float2 p=World.xy+Origin.xy;return smoothstep(.23,.47,f.n(p*.18)*.68+f.n(p*.037)*.32);')
    inputs['PaintWear']=wear
    world=node(unreal.MaterialExpressionWorldPosition);inputs['World']=world
    local_uv=node(unreal.MaterialExpressionTextureCoordinate);inputs['LocalUV']=local_uv
    link(world,'',wear,'World');link(origin,'',wear,'Origin');link(coat,'',wear,'Coat')
    values=[]
    for index in range(8):
        value=node(unreal.MaterialExpressionPerInstanceCustomData);value.set_editor_property('data_index',index);values.append(value)
    def append(a,b):
        result=node(unreal.MaterialExpressionAppendVector);link(a,'',result,'A');link(b,'',result,'B');return result
    data=append(append(values[0],values[1]),append(values[2],values[3]))
    interpolator=node(unreal.MaterialExpressionVertexInterpolator);link(data,'',interpolator,'VS');inputs['Boundary']=interpolator
    edge_data=append(append(values[4],values[5]),append(values[6],values[7]))
    edge_interpolator=node(unreal.MaterialExpressionVertexInterpolator);link(edge_data,'',edge_interpolator,'VS');inputs['Edges']=edge_interpolator
    custom=node(unreal.MaterialExpressionCustom)
    custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('inputs',[custom_input(key) for key in inputs])
    custom.set_editor_property('additional_outputs',[custom_output('Normal',unreal.CustomMaterialOutputType.CMOT_FLOAT3),custom_output('Roughness',unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
    custom.set_editor_property('code', (ROOT/'surface-material.ush').read_text(encoding='utf-8'))
    for key,value in inputs.items():link(value,'',custom,key)
    library.connect_material_property(custom,'',unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(custom,'Normal',unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(custom,'Roughness',unreal.MaterialProperty.MP_ROUGHNESS)
    metallic=node(unreal.MaterialExpressionConstant);metallic.set_editor_property('r',0);library.connect_material_property(metallic,'',unreal.MaterialProperty.MP_METALLIC)
    specular=node(unreal.MaterialExpressionConstant);specular.set_editor_property('r',.2);library.connect_material_property(specular,'',unreal.MaterialProperty.MP_SPECULAR)
    asset.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    mask=node(unreal.MaterialExpressionTextureObjectParameter)
    mask.set_editor_property('parameter_name','BakedMask')
    mask.set_editor_property('texture',unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    mask.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    region=node(unreal.MaterialExpressionVectorParameter);region.set_editor_property('parameter_name','BakedRegion')
    enabled=node(unreal.MaterialExpressionScalarParameter);enabled.set_editor_property('parameter_name','BakedMaskEnabled');enabled.set_editor_property('default_value',0)
    visibility=node(unreal.MaterialExpressionCustom);visibility.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    visibility.set_editor_property('inputs',[custom_input(key) for key in ['Mask','World','Region','Enabled','Visible','Coat','Wear','Boundary','LocalUV']])
    visibility.set_editor_property('code','if (Visible<.5) return 0; float edge=Boundary.w>0&&Boundary.w<.05?saturate((dot(Boundary.xy,LocalUV)+Boundary.z)/Boundary.w):1; float cover=lerp(1,Wear,saturate(Coat))*edge;if (Enabled<.5) return cover; float2 uv=(floor(World.xy*.01)-Region.xy+.5)/256; return Texture2DSampleLevel(Mask,MaskSampler,uv,0).r*cover;')
    for key,value in [('Mask',mask),('World',world),('Region',region),('Enabled',enabled),('Visible',visible),('Coat',coat),('Wear',wear),('Boundary',interpolator),('LocalUV',local_uv)]:link(value,'',visibility,key)
    dither=node(unreal.MaterialExpressionMaterialFunctionCall)
    dither.set_editor_property('material_function',unreal.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
    link(visibility,'',dither,'Alpha Threshold')
    library.connect_material_property(dither,'',unreal.MaterialProperty.MP_OPACITY_MASK)

    library.set_material_usage(asset,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    library.recompile_material(asset);unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def main():
    sources = json.loads((ROOT/'materials/sources.json').read_text(encoding='utf-8'))
    imported = {}
    for asset in sources['assets']:
        maps = dict((kind, texture(value, kind)) for kind, value in asset['maps'].items())
        texture_policy.surface_maps(maps)
        imported[asset['id']] = (maps, asset['widthMeters'])
    styles = {
        'Road_01': ('asphalt001', (.65,.65,.65)), 'Road_02': ('asphalt001', (.42,.43,.44)),
        'Road_03': ('asphalt001', (.85,.85,.85)), 'Road_04': ('gravelly_sand', (.9,.9,.9)),
        'Road_05': ('asphalt001', (.40,.41,.43)), 'Road_06': ('asphalt001', (.36,.37,.39)),
        'Road_07': ('asphalt001', (.75,.72,.67)),
        'Grass_Dark': ('grass005', (.27,.32,.22)), 'Grass_Medium': ('grass005', (.27,.32,.22)),
        'Grass_Light': ('grass005', (.27,.32,.22)), 'Grass': ('grass005', (.27,.32,.22)),
        'Concrete': ('concrete_floor', (1,1,1)), 'Curb': ('concrete_floor', (1.4,1.4,1.35)),
        'Gravel': ('gravelly_sand', (.75,.75,.75)), 'Dirt': ('forest_ground_04', (.7,.63,.53)),
        'Clay': ('brown_mud_02', (.85,.65,.5)), 'Dirt_Grass': ('forest_ground_04', (.75,.8,.55)),
        'Burnt': ('forest_ground_04', (.13,.12,.10)),
        'Sand': ('gravelly_sand', (1.1,1,.8)), 'Unknown': ('concrete_floor', (.6,.6,.6)),
        'PaintWhite': ('asphalt001', (.65,.65,.58)), 'PaintYellow': ('asphalt001', (.72,.52,.025)),
        'PaintHidden': ('asphalt001',(0,0,0)),
    }
    materials = {}
    for name, (identity, tint) in styles.items():
        maps, width = imported[identity]
        materials[name] = material(name, maps, width, tint)
    path = DESTINATION+'/GroundLibrary'
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not asset:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.HeyZedGroundLibrary)
        asset = tools.create_asset('GroundLibrary', DESTINATION, unreal.HeyZedGroundLibrary, factory)
    import runpy
    asset.set_editor_property('recipe', runpy.run_path(str(ROOT/'prepare_world.py'))['recipe']())
    asset.set_editor_property('materials', materials)
    surfaces={}
    for name,(identity,tint) in styles.items():
        maps,width=imported[identity]
        style=unreal.HeyZedSurfaceStyle()
        for key,value in dict(diffuse=maps['diff'],normal=maps['nor_dx'],roughness=maps['rough'],height=maps.get('height',unreal.load_asset('/Engine/EngineResources/Gray')),width=width,tint=unreal.LinearColor(*tint,1)).items():style.set_editor_property(key,value)
        surfaces[name]=style
    asset.set_editor_property('surfaces',surfaces)
    baked_path=DESTINATION+'/Baked/Materials'
    if unreal.EditorAssetLibrary.does_directory_exist(baked_path):
        pairs={a+'_'+b:(a,b) for a in styles for b in styles}
        def soft_surface(name): return name.startswith('Grass') or name in ('Dirt','Dirt_Grass','Sand','Clay','Gravel','Road_04')
        for material_path in unreal.EditorAssetLibrary.list_assets(baked_path,recursive=True,include_folder=False):
            name=material_path.split('.')[0].rsplit('/',1)[-1]
            pair=pairs.get(name.removeprefix('MI_')) if name.startswith('MI_') else None
            if pair:
                instance=unreal.load_asset(material_path)
                library.set_material_instance_scalar_parameter_value(instance,'GroundSoftBlend',float(all(soft_surface(part) for part in pair)))
                library.set_material_instance_scalar_parameter_value(instance,'OverlayRoad',float(pair[1].startswith('Road_') and pair[1]!='Road_04'))
                other=surfaces[pair[1]]
                for parameter,field in [('OverlayDiffuse','diffuse'),('OverlayNormal','normal'),('OverlayRoughness','roughness'),('OverlayHeight','height')]:
                    library.set_material_instance_texture_parameter_value(instance,parameter,other.get_editor_property(field))
                library.set_material_instance_vector_parameter_value(instance,'OverlayTint',other.get_editor_property('tint'))
                library.set_material_instance_scalar_parameter_value(instance,'OverlayWidth',other.get_editor_property('width')*100)
                unreal.EditorAssetLibrary.save_loaded_asset(instance)
    if '--reuse-lawn' in unreal.SystemLibrary.get_command_line():
        grass_material=unreal.load_asset('/Game/HeyZed/Materials/M_GrassClump')
        grass_mesh=unreal.load_asset('/Game/HeyZed/Vegetation/SM_GrassClump')
        if not grass_material or not grass_mesh:raise RuntimeError('Shared lawn resources are not prepared')
        asset.set_editor_property('grass_material',grass_material)
        asset.set_editor_property('grass_mesh',grass_mesh)
    else:
        asset.set_editor_property('grass_material',prepare_lawn.material(*imported['grass005'], styles['Grass'][1]))
        if not asset.prepare_grass(): raise RuntimeError('Grass mesh authoring failed')
    import ground_geometry
    authored=ground_geometry.compile_catalog()
    definitions={}
    for name,source in authored['meshes'].items():
        definition=unreal.HeyZedGroundShape()
        definition.set_editor_property('material',source['material'])
        definition.set_editor_property('vertices',[unreal.Vector2D(*p) for p in source['vertices']])
        definition.set_editor_property('boundary',unreal.Vector4(*source['boundary']))
        definitions[name]=definition
    for name in styles:
        definition=unreal.HeyZedGroundShape()
        definition.set_editor_property('material',name)
        definition.set_editor_property('vertices',[unreal.Vector2D(*p) for p in [[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]]])
        definitions['GroundBase_'+name]=definition
    tiles={}
    for name,parts in authored['tiles'].items():
        tile=unreal.HeyZedGroundTile();tile.set_editor_property('parts',parts);tiles[name]=tile
    asset.set_editor_property('tiles',tiles)
    asset.set_editor_property('overlays',definitions)
    if not asset.prepare_meshes(): raise RuntimeError('Ground mesh authoring failed')
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):raise RuntimeError('Ground library publication failed')
    if not unreal.EditorAssetLibrary.does_asset_exist(DESTINATION+'/World'):
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        unreal.EditorLoadingAndSavingUtils.save_map(world, DESTINATION+'/World')
    unreal.EditorAssetLibrary.save_directory(DESTINATION, only_if_is_dirty=True, recursive=True)
    unreal.log('heyzed native assets prepared: '+str(len(materials))+' materials, '+str(len(definitions))+' floor overlays')
    (ROOT/'client/Saved/HeyZed').mkdir(parents=True, exist_ok=True)
    (ROOT/'client/Saved/HeyZed/assets.json').write_text(json.dumps(dict(materials=len(materials), overlays=len(definitions))))


main()
if '--ground-only' not in unreal.SystemLibrary.get_command_line() and (ROOT/'animation-sample.json').exists():
    import runpy
    runpy.run_path(str(ROOT/'prepare_animation.py'))
