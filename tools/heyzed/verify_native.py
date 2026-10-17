"""Exercise native cameras against a static fixture or an already running live bridge."""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
CLIENT = Path(__file__).resolve().parent/'client'
sys.path.insert(0, str(ROOT/'tools/roof-lab'))
from pzdata import header, read_chunk


def fixture(x=8107, y=11574):
    game = Path('C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid/media/maps/Muldraugh, KY')
    catalog = json.loads((ROOT/'tools/resources-lab/.local/catalog.json').read_text(encoding='utf-8'))
    props = {r['name']:r['properties'] for r in catalog['rows'] if r['kind']=='tile'}
    dictionary, indices, cells = [], {}, []
    for cx in range((x-24)//256, (x+24)//256+1):
        for cy in range((y-24)//256, (y+24)//256+1):
            h = header(game/f'{cx}_{cy}.lotheader')
            pack = (game/f'world_{cx}_{cy}.lotpack').read_bytes()
            for wx in range(max(cx*32,(x-24)//8),min((cx+1)*32,(x+24)//8+1)):
                for wy in range(max(cy*32,(y-24)//8),min((cy+1)*32,(y+24)//8+1)):
                    for px,py,pz,room,names in read_chunk(pack,h,wx,wy):
                        if pz != 0 or abs(px-x)>24 or abs(py-y)>24: continue
                        layers = []
                        for name in names:
                            p = props.get(name,{})
                            if not ('solidfloor' in p or 'FloorOverlay' in p): continue
                            if name not in indices:
                                indices[name]=len(dictionary)
                                dictionary.append(dict(name=name,material=p.get('FloorMaterial',p.get('FootstepMaterial','')),
                                                       overlay='FloorOverlay' in p,edges=''.join(e for e in 'NESW' if 'FloorAttachment'+e in p)))
                            layers.append(indices[name])
                        if layers: cells.append([px,py,layers])
    print(json.dumps(dict(fixture_cells=len(cells),source='installed static PZ map, not live game state')),flush=True)
    return dict(schema=1,epoch=12345,revision=1,dictionary=dictionary,cells=cells)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--engine',default='C:/Program Files/Epic Games/UE_5.8')
    parser.add_argument('--live',action='store_true',help='Use the running PZ adapter on port 28761')
    args=parser.parse_args()
    udp=None
    if not args.live:
        raw=json.dumps(fixture(),separators=(',',':')).encode()
        world=b'PJG1'+struct.pack('>I',len(raw))+zlib.compress(raw)
        assert len(world)<60000
        udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        udp.bind(('127.0.0.1',28762));udp.settimeout(.05)
    output=CLIENT/'Saved/HeyZed'
    for p in output.glob('camera-*.png'): p.unlink()
    process=subprocess.Popen([str(Path(args.engine)/'Engine/Binaries/Win64/UnrealEditor.exe'),
                              str(CLIENT/'HeyZed.uproject'),'/Game/HeyZed/World','-game','-RenderOffscreen',
                              '-Windowed','-ResX=480','-ResY=720','-NoSound','-NoSplash','-Unattended',
                              f'-HeyZedPort={28761 if args.live else 28762}','-HeyZedCameraTest'],creationflags=subprocess.CREATE_NO_WINDOW)
    deadline=time.perf_counter()+300
    count=0;next_world=0
    try:
        while time.perf_counter()<deadline:
            if process.poll() is not None: raise RuntimeError('Native client exited before verification')
            if udp:
                try: packet,peer=udp.recvfrom(1024)
                except (socket.timeout,ConnectionResetError): continue
                fields=packet.decode().split()
                if len(fields)!=9 or fields[0]!='I2': continue
                count+=1
                response=f'S3 12345 {count*2} {fields[1]} {fields[2]} {fields[3]} 0 8107.5 11574.5 0 -0.70710677 -0.70710677 0 0 10000 87,83,65,68,340,342,67 {time.monotonic_ns()} 1.5 2.0 1'
                udp.sendto(response.encode(),peer)
                if time.perf_counter()>=next_world:
                    udp.sendto(world,peer);next_world=time.perf_counter()+1
            else:
                time.sleep(.05)
            if all((output/f'camera-{i}.png').exists() for i in range(5)):
                state=json.loads((output/'health.json').read_text())
                if state['camera_mode'] != 0: continue
                assert abs((state['camera_yaw']+45+180)%360-180)<.01,state
                assert abs((state['camera_pitch']+30+180)%360-180)<.01,state
                assert state['ground_tiles']>0,state
                assert state['width']==480 and state['height']==720,state
                assert state['state_seq']>0,state
                assert state['pid']==process.pid and state['state_age']<1,state
                metrics=state['metrics']
                assert metrics['status']=='LIVE',metrics
                assert metrics['fps']>0 and metrics['tx_hz']>0 and metrics['rx_hz']>0,metrics
                assert metrics['ack_rtt_ms']['mean']>=0,metrics
                assert metrics['ack_rtt_ms']['max']>=metrics['ack_rtt_ms']['p95']>=0,metrics
                assert metrics['ack_rtt_ms']['max']>=metrics['ack_rtt_ms']['mean'],metrics
                if not args.live:
                    assert abs(metrics['pz_input_age_ms']['mean']-10)<.001,metrics
                    assert metrics['skipped_states_total']==metrics['received_states_total']-1,metrics
                    motion=state['locomotion']
                    assert motion['confirmed_motion'] and motion['crouched'],motion
                    assert abs(motion['native_speed_cm_s']-250)<.001,motion
                    assert abs(motion['native_turn_deg_s'])<.001,motion
                    assert motion['visible_speed_cm_s']<1,motion
                log=max((CLIENT/'Saved/Logs').glob('*.log'),key=lambda p:p.stat().st_mtime).read_text(encoding='utf-8',errors='replace')
                assert 'Failed to compile Material' not in log,'Native material compilation failed'
                assert 'heyzed bridge ready on loopback' not in log,'Obsolete Python runtime is still loaded'
                print(json.dumps(dict(status='complete',source='live PZ' if args.live else 'static fixture',confirmed_states=state['state_seq'],health=state,images=[str(output/f'camera-{i}.png') for i in range(5)]),indent=2))
                return
        raise RuntimeError('Native camera/map fixture timed out')
    finally:
        process.terminate()
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        if udp: udp.close()


if __name__=='__main__':main()
