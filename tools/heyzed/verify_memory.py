"""Cross-process Java -> native Unreal shared-memory and addressed scene checks."""
import json,os,subprocess,time
from pathlib import Path
from verify_native import ROOT,CLIENT
PY=Path(__file__)
JAVA=Path('C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid/jre64/bin/java.exe')
GAME=JAVA.parents[2]

def main():
    build=ROOT/'.build/heyzed/memory-verification';build.mkdir(parents=True,exist_ok=True)
    src=ROOT/'tools/heyzed/adapter/Contents/mods/HeyZed/42.20.4/java/src/com/pavelvoronin/heyzed/UnrealMemory.java'
    subprocess.run([str(JAVA),'-jar',str(Path(os.environ['TEMP'])/'PZModBuild/ecj-3.44.0.jar'),'-25','-proc:none','-cp',str(GAME/'projectzomboid.jar'),'-d',str(build),str(src),str(ROOT/'tools/heyzed/tests/MemoryFixture.java')],check=True)
    fixture=subprocess.Popen([str(JAVA),'-cp',str(build)+';'+str(GAME/'projectzomboid.jar'),'com.pavelvoronin.heyzed.MemoryFixture',str(build/'memory')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    ready=fixture.stdout.readline();assert ready.startswith('ready '),ready
    epoch=int(ready.split()[1]);client=None
    def send(channel,payload=None,**extra):
        fixture.stdin.write(json.dumps(dict(channel=channel,**extra,**({'payload':payload} if payload is not None else {})))+'\n');fixture.stdin.flush()
    health_path=CLIENT/'Saved/HeyZed/health.json'
    def health():
        try:
            h=json.loads(health_path.read_text())
            return h if h['pid']==client.pid else {}
        except (OSError,ValueError):return {}
    def wait(predicate,seconds=30):
        deadline=time.monotonic()+seconds;h={}
        while time.monotonic()<deadline:
            if client.poll() is not None:raise RuntimeError('Native client exited')
            h=health()
            if h and predicate(h):return h
            time.sleep(.05)
        raise TimeoutError(h)
    def door(id='door-a',revision=1,deleted=False,open=False):
        return dict(type='object',epoch=epoch,id=id,generation=epoch,revision=revision,kind='door',x=100,y=100,z=0,open=open,north=True,deleted=deleted)
    def ground(name,revision,cells):
        return dict(schema=1,epoch=epoch,revision=revision,dictionary=[dict(name=name,material='Grass',overlay=False,edges='')],cells=cells)
    try:
        client=subprocess.Popen(['C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',str(CLIENT/'HeyZed.uproject'),'/Game/HeyZed/World','-game','-RenderOffscreen','-Windowed','-ResX=480','-ResY=720','-NoSound','-NoSplash','-Unattended','-HeyZedInteractionTest',f'-HeyZedMemoryRoot={build / "memory"}'],creationflags=subprocess.CREATE_NO_WINDOW)
        wait(lambda h:h['state_seq']>0 and h['transport']=='shared_memory',90)
        send('world',ground('a',1,[[100,100,[0]],[101,100,[0]]]))
        h=wait(lambda h:h['ground_tiles']==2);base=h['rebuilt_cells'];assert base==2,h
        send('world',ground('a',2,[[100,100,[0]],[101,100,[0]]]))
        time.sleep(.5);assert health()['rebuilt_cells']==base
        send('world',ground('b',3,[[100,100,[0]]]))
        h=wait(lambda h:h['rebuilt_cells']==base+1);assert h['ground_tiles']==2
        versioned=ground('b',4,[[100,100,[0],2,1]]);versioned['schema']=2
        send('world',versioned)
        time.sleep(.4)
        stale=ground('stale',5,[[100,100,[0],1,1]]);stale['schema']=2
        send('world',stale)
        time.sleep(.4)
        assert health()['rebuilt_cells']==base+1,'Stale cell version replaced newer geometry'
        send('events',door())
        h=wait(lambda h:any(o['id']=='door-a' for o in h['presented_objects']))
        mesh=next(o['mesh_id'] for o in h['presented_objects'] if o['id']=='door-a')
        send('bulk',count=2000)
        time.sleep(.15)
        event_start=time.monotonic()
        send('events',door(revision=2,open=True))
        event_health=wait(lambda h:any(o['id']=='door-a' and o['revision']==2 and o['open'] for o in h['presented_objects']))
        event_observed_ms=(time.monotonic()-event_start)*1000
        assert event_health['world_blocks_consumed']<2005,'Door event was not observed during bulk load'
        assert event_observed_ms<1000,event_observed_ms
        assert next(o['mesh_id'] for o in event_health['presented_objects'] if o['id']=='door-a')==mesh
        samples=[];until=time.monotonic()+4
        while time.monotonic()<until:
            h=health()
            if h:samples.append(h)
            time.sleep(.05)
        wait(lambda h:h['world_blocks_consumed']>=2005)
        assert all(h['state_age']<.5 for h in samples),max(h['state_age'] for h in samples)
        assert health()['rebuilt_cells']==base+1,'Bulk empty patches rebuilt geometry'
        send('events',door(revision=3,deleted=True))
        wait(lambda h:any(o['id']=='door-a' and o['deleted'] for o in h['presented_objects']))
        send('events',door(id='door-b'))
        h=wait(lambda h:any(o['id']=='door-b' and not o['deleted'] for o in h['presented_objects']))
        assert next(o['mesh_id'] for o in h['presented_objects'] if o['id']=='door-b')!=mesh
        new_mesh=next(o['mesh_id'] for o in h['presented_objects'] if o['id']=='door-b')
        send('restart')
        time.sleep(1)
        h=wait(lambda h:h['state_age']<.1)
        assert h['rebuilt_cells']==base+1 and h['ground_tiles']==2
        assert next(o['mesh_id'] for o in h['presented_objects'] if o['id']=='door-b')==new_mesh
        replacement=door(id='door-reconnected');replacement['generation']=epoch+1
        send('events',replacement)
        h=wait(lambda h:any(o['id']=='door-reconnected' for o in h['presented_objects']))
        assert next(o['mesh_id'] for o in h['presented_objects'] if o['id']=='door-reconnected')==new_mesh
        assert next(o['deleted'] for o in h['presented_objects'] if o['id']=='door-b')
        # Native button handler starts a local visual action; fixture rejects it.
        h=wait(lambda h:h['command_ack_ms']>0,20)
        result=dict(status='complete',source='production Java memory publisher + native Unreal',bulk_blocks=2000,bulk_bytes=120000000,
            unchanged_cells_rebuilt=0,changed_cells_rebuilt=1,retained_on_reconnect=True,recreated_identity=True,
            bulk_state_age_max_ms=max(h['state_age'] for h in samples)*1000,
            event_observed_ms=event_observed_ms,event_bulk_remaining=2005-event_health['world_blocks_consumed'],
            bulk_ack_rtt_p95_max_ms=max(h['metrics']['ack_rtt_ms']['p95'] for h in samples),command_ack_ms=h['command_ack_ms'])
        (build/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
    finally:
        if client:
            client.terminate()
            try:client.wait(20)
            except subprocess.TimeoutExpired:client.kill();client.wait()
        if fixture.poll() is None:
            send('quit');fixture.wait(10)
if __name__=='__main__':main()
