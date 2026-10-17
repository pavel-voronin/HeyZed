"""Native MeshSimplify + VT sample only; never saves or changes the working map."""
import hashlib
import json
from pathlib import Path
import unreal
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from prepare_ground_hlod_preview import prepare_preview
data = json.loads((ROOT/'.local/ground-hlod-sample/current.json').read_text('utf-8'))
recipe = Path(data['recipe'])
if hashlib.sha256(recipe.read_bytes()+recipe.with_suffix('.grid').read_bytes()).hexdigest() != data['input_sha256']:
    raise RuntimeError('Ground HLOD sample input changed')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = actors.spawn_actor_from_class(unreal.HeyZedBakedRegion, unreal.Vector())
preview_saved = False
try:
    if not actor or not actor.author(str(recipe), data['identity']):
        raise RuntimeError('Ground HLOD sample source authoring failed')
    if not unreal.HeyZedGroundHLODAuthor.build_sample(actor, data['package'], data['report'], 38400):
        raise RuntimeError('Native ground HLOD sample failed; working world remains unchanged')
    prepare_preview(data, actor)
    preview_saved = True
finally:
    if actor and not preview_saved:
        actors.destroy_actor(actor)
unreal.log('heyzed native ground HLOD sample saved; map migration and visual acceptance remain pending')
