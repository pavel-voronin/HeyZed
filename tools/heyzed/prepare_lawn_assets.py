"""Publish only the shared lawn resources; do not rebuild terrain or the world."""
from pathlib import Path
import sys
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_lawn

asset = unreal.load_asset('/Game/HeyZed/GroundLibrary')
if not asset:
    raise RuntimeError('Prepare the ground material library before the lawn')
style = asset.get_editor_property('surfaces')['Grass']
maps = {'diff': style.get_editor_property('diffuse')}
color = style.get_editor_property('tint')
asset.set_editor_property('grass_material', prepare_lawn.material(maps, style.get_editor_property('width'), (color.r, color.g, color.b)))
if not asset.prepare_grass():
    raise RuntimeError('Bermuda lawn mesh authoring failed')
unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('heyzed Bermuda lawn resources published')
