"""Author a prepared floor base as external actors in an Unreal World Partition map."""
import gc
import hashlib
import json
import os
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parent
import sys
sys.path.insert(0,str(ROOT))
current_path=ROOT/'client/Saved/HeyZed/baked-world.json'
source=json.loads((ROOT/'.local/world/current.json').read_text(encoding='utf-8'))
if current_path.exists():
    current=json.loads(current_path.read_text(encoding='utf-8'))
    if current['identity']!=source['identity']:raise RuntimeError('Source map changed; explicit map migration is required')
    import author_ground_regions
else:
    manifest = json.loads((ROOT/'.local/world/current.json').read_text(encoding='utf-8'))
    prepared=json.loads((ROOT/'.local/authored-regions/current.json').read_text(encoding='utf-8'))
    if prepared['identity']!=manifest['identity']:raise RuntimeError('Authored geometry and native map facts differ')
    folder = Path(prepared['folder'])
    digest = hashlib.sha256(b'heyzed-world-author-v1')
    for source in [Path(__file__),ROOT/'client/Source/HeyZed/HeyZedBakedRegion.cpp',ROOT/'client/Source/HeyZed/HeyZedBakedRegion.h',ROOT/'surface-material.ush']:
        digest.update(source.read_text(encoding='utf-8').encode())
    author_recipe = digest.hexdigest()
    requested_recipe = author_recipe
    alias_path = folder/'unreal-current.json'
    world_path = manifest['world'].removesuffix('/World')+'_'+author_recipe[:12]+'/World'
    map_file = ROOT/'client/Content'/world_path.removeprefix('/Game/')
    map_file = map_file.with_suffix('.umap')
    progress_path = folder/('unreal-authoring-'+author_recipe[:12]+'.json')
    resume = os.environ.get('HEYZED_BAKE_RESUME')
    if not resume and alias_path.exists():
        alias = json.loads(alias_path.read_text())
        if alias.get('recipe') == requested_recipe: resume = alias['progress']
    if resume:
        progress_path = Path(resume).resolve()
        if progress_path.parent != folder.resolve(): raise RuntimeError('Resume state belongs to another source base')
        previous = json.loads(progress_path.read_text())
        if previous['identity'] != manifest['identity']: raise RuntimeError('Resume source identity differs')
        world_path, author_recipe = previous['world'], previous['recipe']
        map_file = (ROOT/'client/Content'/world_path.removeprefix('/Game/')).with_suffix('.umap')
    progress = json.loads(progress_path.read_text()) if progress_path.exists() else dict(identity=manifest['identity'], cells=[], complete=False)
    done = set(progress['cells'])


    def publish(complete=False):
        progress.update(cells=sorted(done), complete=complete, world=world_path, recipe=author_recipe)
        pending = progress_path.with_suffix('.pending')
        pending.write_text(json.dumps(progress, separators=(',', ':')), encoding='utf-8')
        os.replace(pending, progress_path)
        pending_alias = alias_path.with_suffix('.pending')
        pending_alias.write_text(json.dumps(dict(recipe=requested_recipe, progress=str(progress_path)), separators=(',', ':')), encoding='utf-8')
        os.replace(pending_alias, alias_path)
        if complete:
            target = ROOT/'client/Saved/HeyZed/baked-world.json'
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(dict(identity=manifest['identity'], world=world_path, cells=len(done), source=manifest['totals']), separators=(',', ':')), encoding='utf-8')


    def save(world):
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, world_path):
            raise RuntimeError('Could not save the baked World Partition map')
        if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True):
            raise RuntimeError('Could not save baked external actors')
        publish()


    if progress.get('complete') and map_file.exists():
        publish(True)
        unreal.log('heyzed baked world already prepared: '+str(len(done))+' native regions')
    else:
        if map_file.exists():
            world = unreal.EditorLoadingAndSavingUtils.load_map(str(map_file))
        else:
            world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
            if not unreal.HeyZedBakedRegion.configure_world(world):
                raise RuntimeError('Native World Partition configuration failed')
            save(world)
            world = unreal.EditorLoadingAndSavingUtils.load_map(str(map_file))
        if not world: raise RuntimeError('Could not open the baked World Partition map')
        # Recover committed external actors even if a process stopped before publishing its checkpoint.
        descriptors = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
        if isinstance(descriptors, tuple) and len(descriptors) == 2 and isinstance(descriptors[0], bool): descriptors = descriptors[1]
        if descriptors is None: raise RuntimeError('Could not read native external actor descriptors')
        done = {str(desc.label).removeprefix('heyzed base ') for desc in descriptors if str(desc.label).startswith('heyzed base ')}
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        pending_count = 0
        for entry in manifest['source_files']:
            key = Path(entry[0]).stem
            if key in done: continue
            actor = actors.spawn_actor_from_class(unreal.HeyZedBakedRegion, unreal.Vector(0, 0, 0), transient=False)
            if not actor or not actor.author(str(folder/(key+'.json')), manifest['identity']):
                raise RuntimeError('Failed to author native cell '+key)
            actor.set_actor_label('heyzed base '+key)
            done.add(key)
            pending_count += 1
            if pending_count >= 32:
                save(world)
                unreal.log('heyzed baked World Partition regions: '+str(len(done))+'/'+str(len(manifest['source_files'])))
                # Save external actors, then release the editor's loaded working set through native map loading.
                actor = None
                world = None
                unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
                gc.collect()
                unreal.SystemLibrary.collect_garbage()
                world = unreal.EditorLoadingAndSavingUtils.load_map(str(map_file))
                if not world: raise RuntimeError('Could not resume native map authoring')
                pending_count = 0
        save(world)
        publish(True)
        unreal.log('heyzed baked World Partition map prepared: '+str(len(done))+' native regions')
