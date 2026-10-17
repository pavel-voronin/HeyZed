"""Live Enhanced Input -> shared memory -> vanilla PZ -> native animation check."""
import argparse,json,subprocess,time
from pathlib import Path
from verify_native import CLIENT

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--artifact',type=Path,required=True);args=parser.parse_args()
    status=json.loads((args.artifact/'scenario-result.json').read_text(encoding='utf-8-sig'))
    assert status['status']=='interactive','Live movement requires the manual handoff, not MCP input blocking'
    process=subprocess.Popen(['C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',str(CLIENT/'HeyZed.uproject'),'/Game/HeyZed/World','-game','-RenderOffscreen','-Unattended','-NoSound','-NoSplash','-Windowed','-ResX=480','-ResY=720','-HeyZedLiveMotionTest'],creationflags=subprocess.CREATE_NO_WINDOW)
    rows=[];deadline=time.monotonic()+150
    try:
        while time.monotonic()<deadline:
            if process.poll() is not None:raise RuntimeError('Native client exited')
            time.sleep(.1)
            try:h=json.loads((CLIENT/'Saved/HeyZed/health.json').read_text())
            except (OSError,ValueError):continue
            if h['pid']!=process.pid:continue
            if h['live_test_stage']>=0:rows.append(h)
            if h['live_test_stage']>=13:
                gaits={r['locomotion']['gait'] for r in rows}
                assert {0,1,2,3}<=gaits,gaits
                assert any(r['locomotion']['crouched'] and r['locomotion']['visible_speed_cm_s']>10 for r in rows)
                assert any(r['live_test_stage']==5 and r['locomotion']['gait']==3 for r in rows),'No sprint toward camera'
                assert all(r['locomotion']['motion_matching'] for r in rows if r['live_test_stage']>0)
                assert all(r['locomotion']['actor_z']==0 for r in rows)
                assert h['transport']=='shared_memory' and h['ground_tiles']>0
                result=dict(status='complete',source='native Enhanced Input + production shared memory + live vanilla PZ + native Motion Matching',gaits=sorted(gaits),groups=sorted({r['locomotion']['animation_group'] for r in rows}),sprint_toward_camera=True,crouched_movement=True,actor_z=0,last_metrics=h['metrics'],frames=len(rows))
                (CLIENT/'Saved/HeyZed/live-motion-result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2));return
        raise TimeoutError(rows[-1] if rows else 'No accepted state')
    finally:
        process.terminate()
        try:process.wait(20)
        except subprocess.TimeoutExpired:process.kill();process.wait()
if __name__=='__main__':main()
