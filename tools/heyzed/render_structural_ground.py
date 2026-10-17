"""Render the isolated region offscreen and measure repeated visible/empty windows."""
import json
from pathlib import Path
import statistics
import time
import traceback
import unreal

ROOT = Path(__file__).resolve().parent
manifest = json.loads((ROOT/'.local/ground-structure/current.json').read_text('utf-8'))
FOLDER = Path(manifest['file']).parent
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
meshes = {a.get_actor_label().removeprefix('Structural '): a for a in ACTORS.get_all_level_actors() if a.get_actor_label().startswith('Structural ')}
if set(meshes) != {'near','far','horizon'}:
    raise RuntimeError('The isolated three-level structural sample is required')
LEVEL.editor_set_viewport_realtime(False)
LEVEL.editor_set_viewport_realtime(True)
unreal.HeyZedGroundRegionAuthor.configure_render_viewport(1920,1080)
LEVEL.editor_set_game_view(True)
LEVEL.set_level_viewport_fov(60, unreal.Name('None'))
for command in ('r.VSync 0','t.MaxFPS 0','r.GPUStatsEnabled 1','Slate.bAllowThrottling 0','t.IdleWhenNotForeground 0','r.TextureStreaming 0'):
    unreal.SystemLibrary.execute_console_command(world, command)

windows = [('empty',None),('near','near'),('far','far'),('horizon','horizon'),('horizon','horizon'),('far','far'),('near','near'),('empty',None)]
phase = -1
started = 0
samples = []
records = []
captured = set()
last_tick = time.monotonic()
close_started = None


def select(name, close=False):
    for key, actor in meshes.items():
        actor.set_is_temporarily_hidden_in_editor(key != name)
    actor = meshes[name or 'near']
    origin = actor.get_actor_location()
    if close:
        position = origin+unreal.Vector(16500,3800,1800)
        rotation = unreal.Rotator(pitch=-35,yaw=90)
    else:
        position = origin+unreal.Vector(12800,12800,43000)
        rotation = unreal.Rotator(pitch=-90,yaw=90)
    LEVEL.set_level_viewport_camera_info(position,rotation,unreal.Name('None'))


def summary(rows):
    result = {'samples':len(rows)}
    for key in rows[0]:
        values = sorted(float(row[key]) for row in rows)
        result[key] = {'mean':statistics.mean(values),'p50':statistics.median(values),'p95':values[min(len(values)-1,int(len(values)*.95))]}
    return result


def finish():
    result = dict(complete=True, windows=records, screenshots=sorted(captured),
                  environment='Isolated Editor viewport; fixed camera, same shared materials and lighting; offscreen, uncapped',
                  method='Eight sequential empty/near/far/horizon/horizon/far/near/empty windows; 5 s settle then 8 s samples; screenshots outside timing windows',
                  gpu_scope='Native whole-frame GPU time, including sky and Editor rendering; visible-minus-empty is an estimate, not an isolated GPU pass timer')
    result['quality'] = {name:unreal.SystemLibrary.get_console_variable_float_value(name) for name in ('r.Editor.Viewport.ScreenPercentage','r.Editor.Viewport.ScreenPercentageMode.RealTime','sg.ShadowQuality','sg.GlobalIlluminationQuality','sg.ReflectionQuality','sg.TextureQuality','r.AntiAliasingMethod')}
    grouped = {}
    for record in records:
        grouped.setdefault(record['name'],[]).append(record['summary'])
    result['mean_of_windows'] = {name:{key:statistics.mean(row[key]['mean'] for row in rows) for key in rows[0] if key != 'samples'} for name,rows in grouped.items()}
    baseline = result['mean_of_windows']['empty']
    result['estimated_ground_delta_ms'] = {name:{'gpu_ms':values['gpu_ms']-baseline['gpu_ms']} for name,values in result['mean_of_windows'].items() if name != 'empty'}
    (FOLDER/'render-result.json').write_text(json.dumps(result,indent=2),'utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.HeyZedGroundRegionAuthor.configure_render_viewport(0,0)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(delta):
    global phase,started,samples,last_tick,close_started
    try:
        now = time.monotonic()
        dt = (now-last_tick)*1000
        last_tick = now
        if phase == -1:
            phase = 0;started = now;select(None);return
        if phase >= len(windows):
            if close_started is None:
                select('near',True);close_started=now;return
            if now-close_started > 8 and 'near-street.png' not in captured:
                unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(FOLDER/'near-street.png'))
                captured.add('near-street.png');return
            if now-close_started > 12:
                finish()
            return
        elapsed = now-started
        settle = 25 if phase == 0 else 5
        name, visible = windows[phase]
        if settle < elapsed < settle+8:
            row = json.loads(unreal.HeyZedGroundRegionAuthor.frame_metrics())
            if row.get('viewport_width') != 1920 or row.get('viewport_height') != 1080:
                raise RuntimeError('The measured viewport is not 1920 x 1080')
            if row.get('render_screen_percentage') != 100:
                raise RuntimeError('The measured render resolution is not native 100 percent')
            row['tick_ms'] = dt
            if row['gpu_ms'] > 0:
                samples.append(row)
        if elapsed < settle+8:
            return
        if not samples:
            raise RuntimeError('No native GPU timings were produced')
        if visible and visible+'-overview.png' not in captured:
            filename = visible+'-overview.png'
            unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(FOLDER/filename))
            captured.add(filename)
        if elapsed < settle+12:
            return
        records.append(dict(name=name,summary=summary(samples)))
        phase += 1;samples=[];started=now
        if phase < len(windows):
            select(windows[phase][1])
    except Exception:
        (FOLDER/'render-error.txt').write_text(traceback.format_exc(),'utf-8')
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.HeyZedGroundRegionAuthor.configure_render_viewport(0,0)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
