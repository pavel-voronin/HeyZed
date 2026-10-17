"""Update only existing native floor actors in the current map, preserving other actors."""
from pathlib import Path
import gc
import hashlib
import json
import unreal
import sys

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import ground_geometry
prepared=json.loads((ROOT/'.local/authored-regions/current.json').read_text(encoding='utf-8'))
world=json.loads((ROOT/'client/Saved/HeyZed/baked-world.json').read_text(encoding='utf-8'))
if prepared['identity']!=world['identity']:raise RuntimeError('Native floor fact identity differs from current map')
folder=Path(prepared['folder'])
if ground_geometry.compile_catalog()!=json.loads((folder/'catalog.json').read_text(encoding='utf-8')):raise RuntimeError('Authored profiles changed; prepare the regional recipes again before publishing')
digest=hashlib.sha256(b'heyzed-authored-ground-region-v1')
for filename in ['author_ground_regions.py','client/Source/HeyZed/HeyZedBakedRegion.cpp','client/Source/HeyZed/HeyZedBakedRegion.h']:
    digest.update((ROOT/filename).read_bytes())
progress=folder/('native-authoring-'+digest.hexdigest()[:16]+'.json')
done=set(json.loads(progress.read_text(encoding='utf-8')) if progress.exists() else [])
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.load_map(world['world'])
descriptors=unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
if isinstance(descriptors,tuple) and isinstance(descriptors[0],bool):descriptors=descriptors[1]
regions={str(d.label).removeprefix('heyzed base '):d.guid for d in descriptors if str(d.label).startswith('heyzed base ')}
mask_folder=ROOT/'.local/architecture-world/resolved/floor-masks'
pending=[r for r in prepared['regions'] if r not in done]
for start in range(0,len(pending),16):
    batch=pending[start:start+16]
    if any(r not in regions for r in batch):raise RuntimeError('Missing prepared base actor; refusing to rebuild the map')
    unreal.WorldPartitionBlueprintLibrary.load_actors([regions[r] for r in batch])
    loaded={a.get_actor_label().removeprefix('heyzed base '):a for a in actors.get_all_level_actors() if isinstance(a,unreal.HeyZedBakedRegion)}
    for key in batch:
        actor=loaded.get(key)
        if not actor or not actor.author(str(folder/(key+'.json')),world['identity']):raise RuntimeError('Ground authoring failed: '+key)
        mask=mask_folder/(key+'.bin')
        if mask.exists() and not actor.apply_architecture_mask(str(mask)):raise RuntimeError('Invalid architecture floor mask: '+key)
    if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True):raise RuntimeError('Could not save native ground actors')
    done.update(batch);progress.write_text(json.dumps(sorted(done)),encoding='utf-8')
    unreal.log(f'heyzed authored ground actors: {len(done)}/{len(prepared["regions"])}')
    actor=None;loaded.clear()
    unreal.WorldPartitionBlueprintLibrary.unload_actors([regions[r] for r in batch])
    gc.collect();unreal.SystemLibrary.collect_garbage()
unreal.log('heyzed authored ground resource publication complete; visual acceptance remains pending')
