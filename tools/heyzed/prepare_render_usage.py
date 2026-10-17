from pathlib import Path
import sys
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import material_usage

for folder in ('/Game/HeyZed/Architecture/Materials', '/Game/HeyZed/Lighting'):
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        if isinstance(asset, (unreal.Material, unreal.MaterialInstanceConstant)):
            material_usage.require(asset, nanite=True, instanced=True)
        elif isinstance(asset, unreal.StaticMesh) and folder.endswith('Lighting'):
            material_usage.fixture_mesh(asset)
            non_shadow = [i for i, slot in enumerate(asset.get_editor_property('static_materials'))
                          if any(token in str(slot.material_slot_name).lower() for token in ('bulb', 'glass', 'light'))]
            if not unreal.HeyZedLightResources.prepare_fixture_mesh(asset, non_shadow):
                raise RuntimeError('Could not prepare fixture compatibility: ' + path)
            if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
                raise RuntimeError('Could not save fixture compatibility: ' + path)
unreal.log('heyzed native material usage published')
