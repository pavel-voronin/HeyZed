"""Save a separate manual comparison scene from a completed native HLOD sample."""
import json
from pathlib import Path
import time
import unreal

ROOT = Path(__file__).resolve().parent


def prepare_preview(data, actor=None):
    result = json.loads(Path(data['report']).read_text('utf-8-sig'))
    if not result.get('complete'):
        raise RuntimeError('Native HLOD resource is incomplete')
    started = time.monotonic()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if actor is None:
        actor = actors.spawn_actor_from_class(unreal.HeyZedBakedRegion, unreal.Vector())
        if not actor or not actor.author(data['recipe'], data['identity']):
            raise RuntimeError('Ground preview source unavailable')
    actor.set_actor_label('Native source | '+data['region'])
    actor.set_actor_hidden_in_game(False)
    unreal.HeyZedGroundHLODAuthor.configure_preview_view(actor)
    for index, output in enumerate(result['outputs']):
        position, rotation = output['location'], output['rotation']
        proxy = actors.spawn_actor_from_class(unreal.StaticMeshActor,
                    unreal.Vector(position[0]-30000, position[1], position[2]),
                    unreal.Rotator(pitch=rotation[0], yaw=rotation[1], roll=rotation[2]))
        mesh = unreal.load_asset(output['mesh'])
        if not proxy or not mesh:
            raise RuntimeError('Prepared HLOD preview mesh unavailable')
        proxy.set_actor_label('HLOD VT '+str(result['texture_size'])+' | '+str(index))
        component = proxy.get_component_by_class(unreal.StaticMeshComponent)
        component.set_static_mesh(mesh)
        component.set_cast_shadow(False)
        proxy.set_actor_scale3d(unreal.Vector(*output['scale']))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase.static_class())
    sunlight = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(), unreal.Rotator(pitch=-45, yaw=-35))
    sun = sunlight.get_component_by_class(unreal.DirectionalLightComponent)
    sun.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.set_intensity(100000)
    sun.set_editor_property('atmosphere_sun_light', True)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector())
    skylight = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector())
    sky = skylight.get_component_by_class(unreal.SkyLightComponent)
    sky.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('real_time_capture', True)
    exposure = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector())
    exposure.set_editor_property('unbound', True)
    settings = exposure.get_editor_property('settings')
    settings.set_editor_property('override_auto_exposure_min_brightness', True)
    settings.set_editor_property('override_auto_exposure_max_brightness', True)
    settings.set_editor_property('auto_exposure_min_brightness', 15)
    settings.set_editor_property('auto_exposure_max_brightness', 15)
    exposure.set_editor_property('settings', settings)
    preview_world = data['package']+'_Preview_'+str(result['texture_size'])
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, preview_world):
        raise RuntimeError('Separate ground preview map could not be saved')
    preview_file = Path(unreal.Paths.project_content_dir()) / (preview_world.removeprefix('/Game/')+'.umap')
    result.update(preview_complete=True, preview_world=preview_world,
                  preview_prepare_and_save_seconds=time.monotonic()-started,
                  preview_map_bytes=preview_file.stat().st_size)
    Path(data['report']).write_text(json.dumps(result, indent=2), 'utf-8')


if __name__ == '__main__':
    prepare_preview(json.loads((ROOT/'.local/ground-hlod-sample/current.json').read_text('utf-8')))
