"""Publish native shared woody meshes and foliage materials, leaving the world intact."""
from pathlib import Path
import json
import sys
import unreal

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import prepare_woody_sources
import texture_policy

prepare_woody_sources.expand()
tools=unreal.AssetToolsHelpers.get_asset_tools();edit=unreal.MaterialEditingLibrary
FOLDER='/Game/HeyZed/Vegetation'


def texture(filename,kind):
    source=prepare_woody_sources.SOURCE/'textures'/filename
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(source),destination_path=FOLDER+'/Textures',destination_name=source.stem,
                          automated=True,replace_existing=True,save=True).items():task.set_editor_property(key,value)
    tools.import_asset_tasks([task]);asset=unreal.load_asset(FOLDER+'/Textures/'+source.stem)
    texture_policy.surface_texture(asset,'nor_dx' if kind=='normal' else 'diff' if kind=='diff' else 'rough')
    if kind=='normal':asset.set_editor_property('flip_green_channel',True)
    if kind=='alpha':
        asset.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
        asset.set_editor_property('do_scale_mips_for_alpha_coverage',True)
        asset.set_editor_property('alpha_coverage_thresholds',unreal.Vector4(.35,.35,.35,.35))
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):raise RuntimeError('Could not publish vegetation texture')
    return asset


def material(name,files,foliage=False):
    path=FOLDER+'/Materials/M_'+name
    asset=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_'+name,FOLDER+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(asset)
    asset.set_editor_property('two_sided',foliage);asset.set_editor_property('dithered_lod_transition',True)
    asset.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE if foliage else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    asset.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED if foliage else unreal.BlendMode.BLEND_OPAQUE)
    asset.set_editor_property('opacity_mask_clip_value',.35)
    def node(cls):return edit.create_material_expression(asset,cls)
    def link(a,out,b,pin):
        if not edit.connect_material_expressions(a,out,b,pin):raise RuntimeError('Vegetation material pin: '+pin)
    def output(a,out,prop):
        if not edit.connect_material_property(a,out,prop):raise RuntimeError('Vegetation material output: '+str(prop))
    samples={}
    for kind,filename in files.items():
        sample=node(unreal.MaterialExpressionTextureSampleParameter2D);sample.set_editor_property('parameter_name',kind)
        sample.set_editor_property('texture',texture(filename,kind));sample.set_editor_property('sampler_type',
            unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='normal' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind=='diff'
            else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if kind=='alpha' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        samples[kind]=sample
    output(samples['diff'],'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
    output(samples['normal'],'RGB',unreal.MaterialProperty.MP_NORMAL)
    output(samples['arm'],'R',unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    output(samples['arm'],'G',unreal.MaterialProperty.MP_ROUGHNESS)
    spec=node(unreal.MaterialExpressionConstant);spec.set_editor_property('r',.2)
    output(spec,'',unreal.MaterialProperty.MP_SPECULAR)
    if foliage:
        color=node(unreal.MaterialExpressionMultiply);link(samples['diff'],'RGB',color,'A');color.set_editor_property('const_b',.35)
        output(color,'',unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        fade=node(unreal.MaterialExpressionPerInstanceFadeAmount)
        dither=node(unreal.MaterialExpressionMaterialFunctionCall)
        dither.set_material_function(unreal.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
        link(fade,'',dither,'Alpha Threshold')
        alpha=node(unreal.MaterialExpressionMultiply);link(samples['alpha'],'R',alpha,'A');link(dither,'',alpha,'B')
        output(alpha,'',unreal.MaterialProperty.MP_OPACITY_MASK)
    edit.set_material_usage(asset,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):raise RuntimeError('Could not publish vegetation material')
    return asset


def maps(prefix,alpha=False):
    result={kind:prefix+'_'+suffix+'_2k.jpg' for kind,suffix in [('diff','diff'),('normal','nor_gl'),('arm','arm')]}
    if alpha:result['alpha']=prefix+'_alpha_2k.png'
    return result


if unreal.EditorAssetLibrary.does_asset_exist(FOLDER+'/WoodyLibrary'):
    library=unreal.load_asset(FOLDER+'/WoodyLibrary')
else:
    factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.HeyZedVegetationLibrary)
    library=tools.create_asset('WoodyLibrary',FOLDER,unreal.HeyZedVegetationLibrary,factory)
materials={'Tree':[material('TreeBranches',maps('tree_small_02_branch')),
                   material('TreeLeaves',maps('tree_small_02_leaves',True),True),
                   material('TreeTrunk',maps('tree_small_02'))],
           'Shrub':[material('Shrub',maps('shrub_04',True),True)],
           'Pine':[material('PineBark',maps('pine_tree_01_bark')),
                   material('PineTrunk',maps('pine_tree_01_trunk_a')),
                   material('PineNeedles',maps('pine_tree_01_twig',True),True),
                   material('PineDeadBranches',maps('pine_tree_01_trunk_b'))]}
for kind,parts in materials.items():
    if not library.prepare_mesh(kind,str(ROOT/'.local/woody'/(kind+'.json')),parts):raise RuntimeError('Vegetation mesh failed: '+kind)
if not unreal.EditorAssetLibrary.save_loaded_asset(library):raise RuntimeError('Could not publish vegetation library')
unreal.log('heyzed native tree and shrub resources published')
