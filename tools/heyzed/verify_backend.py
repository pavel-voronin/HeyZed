"""Check camera-relative vanilla movement in the owned live NoSave bridge scene."""
import json
import argparse
import math
from pathlib import Path
import secrets
import socket
import time
import zlib


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--artifact',required=True,type=Path,help='Owned manual NoSave scenario artifact')
    parser.add_argument('--locomotion',action='store_true',help='Also check native run, sprint, crouch and turn state')
    args=parser.parse_args()
    scenario=json.loads((args.artifact/'scenario.json').read_text(encoding='utf-8-sig'))
    assert scenario['id']=='heyzed' and scenario.get('manual'),'Expected the owned bridge test scene'
    deadline=time.monotonic()+180
    while True:
        result=json.loads((args.artifact/'scenario-result.json').read_text(encoding='utf-8-sig'))
        if result['status']=='interactive': break
        if time.monotonic()>deadline: raise RuntimeError('PZ has not handed the prepared scene to the player')
        time.sleep(.25)
    from memory_peer import MemoryPeer
    udp=MemoryPeer()
    udp.settimeout(.025)
    session=secrets.randbits(61)
    sequence=0
    latest=None
    ground=None
    motion=[]
    ground_cells={}
    ground_tiles={}

    def sample(mask,yaw,duration,camera=1):
        nonlocal sequence,latest,ground
        deadline=time.monotonic()+duration
        while time.monotonic()<deadline:
            sequence+=1
            udp.sendto(f'I2 {session} {sequence} 0 {mask} {yaw} {yaw} {camera} -'.encode(),('127.0.0.1',28761))
            try: packet,_=udp.recvfrom(65536)
            except socket.timeout: continue
            if packet[:4]==b'PJG1':
                patch=json.loads(zlib.decompress(packet[8:]))
                for tile in patch['dictionary']:ground_tiles[tile['name']]=tile
                for cell in patch['cells']:ground_cells[tuple(cell[:2])]=cell
                ground=dict(dictionary=list(ground_tiles.values()),cells=list(ground_cells.values()));continue
            fields=packet.decode().split()
            if ((len(fields)==16 and fields[0]=='S') or (len(fields)==22 and fields[0]=='S2') or (len(fields)==20 and fields[0]=='S3')) and int(fields[3])==session:
                latest=(float(fields[7]),float(fields[8]),float(fields[9]))
                if fields[0] in ('S2','S3'):
                    motion.append(dict(gait=int(fields[12]),speed=math.hypot(float(fields[17]),float(fields[18])),yaw=math.atan2(float(fields[11]),float(fields[10])),turn=0,crouched=fields[19 if fields[0]=='S3' else 21]=='1'))
        if latest is None: raise RuntimeError('No authoritative state from live PZ')
        return latest

    results=[]
    initial_crouched=None
    try:
        sample(0,-135,1.5)
        if args.locomotion:
            assert motion,'Updated S3 adapter required for native locomotion checks'
            initial_crouched=motion[-1]['crouched']
            if initial_crouched:
                sample(64,-135,.15);sample(0,-135,.5)
        for yaw in (-135,0,90,-90,180):
            start=sample(0,yaw,.3)
            sample(1,yaw,.7)
            end=sample(0,yaw,.3)
            dx,dy=end[0]-start[0],end[1]-start[1]
            distance=math.hypot(dx,dy)
            angle=math.radians(yaw)
            alignment=(dx*math.cos(angle)+dy*math.sin(angle))/max(distance,1e-9)
            results.append(dict(yaw=yaw,distance=distance,alignment=alignment,z_before=start[2],z_after=end[2]))
            sample(2,yaw,.7)
            sample(0,yaw,.3)
            assert distance>.1 and alignment>.9,results[-1]
            assert start[2]==end[2],results[-1]
        modes=[]
        if args.locomotion:
            for mask,expected,name in ((17,2,'run'),(34,3,'sprint_toward_camera'),(33,3,'sprint')):
                before=sample(0,0,.3);start=len(motion)
                sample(mask,0,1.5)
                observed=motion[start:]
                assert any(s['gait']==expected and s['speed']>.1 for s in observed),(name,observed[-5:])
                after=sample(0,0,.4)
                displacement=after[0]-before[0]
                assert displacement*(-1 if mask&2 else 1)>.1,(name,displacement)
                modes.append(dict(mode=name,max_speed=max(s['speed'] for s in observed),world_dx=displacement))
            sample(64,0,.15);sample(0,0,.5)
            assert motion and motion[-1]['crouched'],'Native crouch did not activate'
            start=len(motion);sample(1,0,.8);sample(0,0,.3)
            assert any(s['crouched'] and s['speed']>.1 for s in motion[start:]),'No crouched movement'
            sample(64,0,.15);sample(0,0,.5)
            assert not motion[-1]['crouched'],'Native crouch did not release'
            sample(0,0,.8,0)
            start=len(motion);sample(0,180,.12,0);early=motion[start:]
            sample(0,180,1.0,0)
            error=lambda yaw:abs(math.degrees(math.atan2(math.sin(yaw-math.pi),math.cos(yaw-math.pi))))
            assert error(motion[-1]['yaw'])<15,motion[-1]
            modes.append(dict(mode='gameplay_facing',final_error_deg=error(motion[-1]['yaw'])))
        assert ground and ground['cells'],'No live ground snapshot'
        overlays=[t['name'] for t in ground['dictionary'] if t['overlay']]
        assert any(n.startswith('street_trafficlines_') for n in overlays),overlays
        assert any(n.startswith('street_curbs_') for n in overlays),overlays
        print(json.dumps(dict(status='complete',source='live PZ',movement=results,locomotion=modes,cells=len(ground['cells']),overlay_types=len(overlays),road_paint=[n for n in overlays if n.startswith('street_trafficlines_')],curbs=[n for n in overlays if n.startswith('street_curbs_')]),indent=2))
    finally:
        if initial_crouched is not None and motion and motion[-1]['crouched'] != initial_crouched:
            sample(64,-135,.15);sample(0,-135,.5)
        sequence+=1
        udp.sendto(f'I2 {session} {sequence} 0 0 -135 -135 1 -'.encode(),('127.0.0.1',28761))
        udp.close()


if __name__=='__main__':main()
