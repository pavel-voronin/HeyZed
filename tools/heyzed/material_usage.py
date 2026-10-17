import unreal


def require(asset, *, nanite=False, instanced=False):
    if not isinstance(asset, (unreal.Material, unreal.MaterialInstanceConstant)):
        return
    if not asset.get_path_name().startswith('/Game/HeyZed/'):
        raise RuntimeError('Material usage must be authored on a project-owned resource')
    edit = unreal.MaterialEditingLibrary
    usages = []
    if instanced:
        usages.append(unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    if nanite and asset.get_blend_mode() in (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED):
        usages.append(unreal.MaterialUsage.MATUSAGE_NANITE)
    changed = False
    for usage in usages:
        if edit.has_material_usage(asset, usage):
            continue
        if isinstance(asset, unreal.MaterialInstanceConstant):
            edit.set_material_usage_override(asset, usage, True, True)
        else:
            edit.set_base_material_usage(asset, usage, True)
        changed = True
    if changed:
        if isinstance(asset, unreal.MaterialInstanceConstant):
            edit.update_material_instance(asset)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
            raise RuntimeError('Could not save material usage: ' + asset.get_path_name())
    for usage in usages:
        if not edit.has_material_usage(asset, usage):
            raise RuntimeError('Material usage was not published: ' + asset.get_path_name())


def fixture_mesh(mesh):
    for slot in mesh.get_editor_property('static_materials'):
        require(slot.material_interface, nanite=True, instanced=True)
