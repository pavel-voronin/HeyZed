import unreal


def publish(resource):
    if unreal.EditorAssetLibrary.get_metadata_tag(resource, 'ConstructionFacing') == 'unreal-front-v1':
        return
    changed = polygons = total = 0
    for path in unreal.EditorAssetLibrary.list_assets('/Game/HeyZed/Architecture/Meshes', recursive=True, include_folder=False):
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        count = unreal.HeyZedArchitectureResources.repair_mesh_facing(mesh)
        if count < 0:
            raise RuntimeError('Construction source mesh unavailable: ' + path)
        total += 1
        if count:
            if not unreal.EditorAssetLibrary.save_loaded_asset(mesh):
                raise RuntimeError('Construction facing save failed: ' + path)
            changed += 1
            polygons += count
        if total % 50 == 0:
            unreal.log('heyzed construction facing: visited=' + str(total) + ' repaired=' + str(changed))
            unreal.SystemLibrary.collect_garbage()
    unreal.EditorAssetLibrary.set_metadata_tag(resource, 'ConstructionFacing', 'unreal-front-v1')
    if not unreal.EditorAssetLibrary.save_loaded_asset(resource):
        raise RuntimeError('Construction facing completion save failed')
    unreal.log('heyzed construction facing complete: meshes=' + str(total) + ' repaired=' + str(changed) + ' polygons=' + str(polygons))
