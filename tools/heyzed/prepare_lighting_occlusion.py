"""Refresh existing construction ray-tracing meshes without rebuilding the world."""
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
assets = registry.get_assets_by_path('/Game/HeyZed/Architecture/Meshes', recursive=True)
count = 0
for asset in assets:
    if str(asset.asset_class_path.asset_name) != 'StaticMesh':
        continue
    mesh = asset.get_asset()
    if not unreal.HeyZedLightResources.prepare_occlusion_mesh(mesh):
        raise RuntimeError(f'Cannot prepare construction occlusion: {asset.package_name}')
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=True)
    count += 1
    mesh = None
    if count % 100 == 0:
        unreal.log(f'heyzed construction occlusion prepared: {count}')
        unreal.SystemLibrary.collect_garbage()
unreal.log(f'heyzed construction occlusion complete: {count} meshes')
