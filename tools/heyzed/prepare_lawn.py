"""Publish a CC0 Bermuda lawn material using its authored leaf silhouettes."""
from pathlib import Path
import json
import unreal
import prepare_bermuda
import texture_policy

ROOT = Path(__file__).resolve().parent


def material(maps, width, tint):
    prepare_bermuda.prepare()
    imported = {}
    files = {'diff': 'grass_bermuda_01_diff_2k.jpg', 'normal': 'grass_bermuda_01_nor_gl_2k.jpg',
             'arm': 'grass_bermuda_01_arm_2k.jpg', 'alpha': 'grass_bermuda_01_alpha_2k.png'}
    for kind, filename in files.items():
        source = prepare_bermuda.SOURCE/'textures'/filename
        task = unreal.AssetImportTask()
        for key, value in dict(filename=str(source), destination_path='/Game/HeyZed/Vegetation/Textures',
                               destination_name=source.stem, automated=True, replace_existing=True, save=True).items():
            task.set_editor_property(key, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset('/Game/HeyZed/Vegetation/Textures/'+source.stem)
        texture_policy.surface_texture(texture, 'nor_dx' if kind=='normal' else 'diff' if kind=='diff' else 'rough')
        if kind=='normal':
            texture.set_editor_property('flip_green_channel', True)
        if kind=='alpha':
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            texture.set_editor_property('do_scale_mips_for_alpha_coverage', True)
            texture.set_editor_property('alpha_coverage_thresholds', unreal.Vector4(.35,.35,.35,.35))
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
        imported[kind] = texture
    library = unreal.MaterialEditingLibrary
    path = '/Game/HeyZed/Materials/M_GrassClump'
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_GrassClump', '/Game/HeyZed/Materials', unreal.Material, unreal.MaterialFactoryNew())
    library.delete_all_material_expressions(asset)
    asset.set_editor_property('two_sided', True)
    asset.set_editor_property('tangent_space_normal', True)
    asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    asset.set_editor_property('opacity_mask_clip_value', .35)
    asset.set_editor_property('dithered_lod_transition', True)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)

    def link(a, output, b, pin):
        if not library.connect_material_expressions(a, output, b, pin):
            raise RuntimeError('Bermuda material connection failed: '+pin)

    def property_link(a, output, property):
        if not library.connect_material_property(a, output, property):
            raise RuntimeError('Bermuda material output connection failed: '+str(property))

    def node(cls):
        return library.create_material_expression(asset, cls)

    def custom(code, inputs, kind):
        value = node(unreal.MaterialExpressionCustom)
        value.set_editor_property('output_type', kind)
        pins = []
        for key in inputs:
            pin = unreal.CustomInput(); pin.set_editor_property('input_name', key); pins.append(pin)
        value.set_editor_property('inputs', pins); value.set_editor_property('code', code)
        for key, expression in inputs.items():
            link(expression, '', value, key)
        return value

    uv = node(unreal.MaterialExpressionTextureCoordinate)
    samples = {}
    for kind, texture in imported.items():
        sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
        sample.set_editor_property('parameter_name', 'Bermuda_'+kind)
        sample.set_editor_property('texture', texture)
        sample.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='normal' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind=='diff' else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if kind=='alpha' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        samples[kind] = sample
    world = node(unreal.MaterialExpressionWorldPosition)
    ground = node(unreal.MaterialExpressionTextureObjectParameter)
    ground.set_editor_property('parameter_name', 'LawnDiffuse'); ground.set_editor_property('texture', maps['diff'])
    ground.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    # Preserve the photographed leaf detail while matching the distant lawn's
    # world-space colour field. Atlas UVs never become a projected ground tile.
    color = custom('''struct F {
float h(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float n(float2 p) { float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(h(i),h(i+float2(1,0)),f.x),lerp(h(i+float2(0,1)),h(i+1),f.x),f.y); }
}; F f;
float2 p=World.xy;
float variation=f.n(p*.0009);
float3 base=lerp(Texture2DSample(Lawn,LawnSampler,p/''' + str(width*100) + ''').rgb,
Texture2DSample(Lawn,LawnSampler,p/''' + str(width*100) + '''*1.37+float2(7.31,3.17)).rgb,variation);
float macro=lerp(.82,1.12,f.n(p*.00025));
base*=float3(''' + ','.join(str(x) for x in tint) + ''');
return lerp(Leaf.rgb*float3(.9,1,.9),base*1.15,.35)*macro;''',
                   {'World': world, 'Leaf': samples['diff'], 'Lawn': ground}, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    property_link(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    subsurface = custom('return Color*.25;', {'Color': color}, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    property_link(subsurface, '', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    property_link(samples['normal'], 'RGB', unreal.MaterialProperty.MP_NORMAL)
    property_link(samples['arm'], 'R', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    rough = custom('return max(.75,ARM.g);', {'ARM': samples['arm']}, unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    property_link(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    specular = node(unreal.MaterialExpressionConstant); specular.set_editor_property('r', .15)
    property_link(specular, '', unreal.MaterialProperty.MP_SPECULAR)
    fade = node(unreal.MaterialExpressionPerInstanceFadeAmount)
    dither = node(unreal.MaterialExpressionMaterialFunctionCall)
    dither.set_material_function(unreal.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
    link(fade, '', dither, 'Alpha Threshold')
    mask = node(unreal.MaterialExpressionMultiply)
    link(samples['alpha'], 'R', mask, 'A')
    link(dither, '', mask, 'B')
    property_link(mask, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    library.set_material_usage(asset, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    library.recompile_material(asset); unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset
