"""Shared import policy for surface textures, excluding semantic/data masks."""
import unreal


def surface_texture(asset, kind):
    asset.set_editor_property('srgb', kind == 'diff')
    asset.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    asset.set_editor_property('never_stream', False)
    asset.set_editor_property('lod_bias', 0)
    asset.set_editor_property('filter', unreal.TextureFilter.TF_DEFAULT)
    group = (unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP if kind == 'nor_dx'
             else unreal.TextureGroup.TEXTUREGROUP_WORLD if kind == 'diff'
             else unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR)
    asset.set_editor_property('lod_group', group)
    if kind == 'nor_dx':
        asset.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)


def surface_maps(maps):
    normal, roughness = maps.get('nor_dx'), maps.get('rough')
    if normal and roughness:
        roughness.set_editor_property('composite_texture', normal)
        roughness.set_editor_property('composite_texture_mode', unreal.CompositeTextureMode.CTM_NORMAL_ROUGHNESS_TO_RED)
        roughness.set_editor_property('composite_power', 1.0)
        unreal.EditorAssetLibrary.save_loaded_asset(roughness)
