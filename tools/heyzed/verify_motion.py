"""Native animation acceptance with authoritative synthetic motion; no OS input."""
import json,math,socket,struct,subprocess,time,zlib
from pathlib import Path
from verify_native import CLIENT,fixture

def main():
    output=CLIENT/'Saved/HeyZed'
    for p in output.glob('motion-*.png'):p.unlink()
    raw=json.dumps(fixture(),separators=(',',':')).encode()
    world=b'PJG1'+struct.pack('>I',len(raw))+zlib.compress(raw)
    udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);udp.bind(('127.0.0.1',28762));udp.settimeout(.05)
    process=subprocess.Popen(['C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',str(CLIENT/'HeyZed.uproject'),'/Game/HeyZed/World','-game','-RenderOffscreen','-Windowed','-ResX=480','-ResY=720','-NoSound','-NoSplash','-Unattended','-HeyZedPort=28762','-HeyZedMotionTest'],creationflags=subprocess.CREATE_NO_WINDOW)
    # Distinct clips must fit moving coordinates, rather than an arbitrary gait speed.
    stages=[('Idle',0,0,False),('Walk',1,170,False),('Run',2,390,False),('Sprint',3,620,False),('Crouch',1,110,True),('Turn',0,0,False),('Idle',0,0,False)]
    stage=0;started=time.monotonic();last=started;deadline=started+360
    x,y=8107.5,11574.5;seq=0;next_world=0;results=[];yaw=-135
    try:
        while time.monotonic()<deadline:
            if process.poll() is not None:raise RuntimeError('Native viewer exited')
            try:data,peer=udp.recvfrom(1024)
            except (socket.timeout,ConnectionResetError):continue
            f=data.decode().split()
            if len(f)!=9:continue
            now=time.monotonic();dt=min(now-last,.05);last=now
            name,gait,speed,crouch=stages[stage]
            if stage in (1,2,3,4):
                direction=(stage-1)*math.pi/2
                vx,vy=math.cos(direction)*speed/100,math.sin(direction)*speed/100
                x+=vx*dt;y+=vy*dt;yaw=math.degrees(direction)
            else:vx=vy=0
            if stage==5:yaw+=90*dt
            angle=math.radians(yaw);seq+=1
            response=f'S3 12345 {seq} {f[1]} {f[2]} {f[3]} 0 {x} {y} 0 {math.cos(angle)} {math.sin(angle)} {gait} 0 1000 87,83,65,68,340,342,67 {time.monotonic_ns()} {vx} {vy} {int(crouch)}'
            udp.sendto(response.encode(),peer)
            if now>=next_world:udp.sendto(world,peer);next_world=now+1
            capture=output/f'motion-{stage}.png'
            if now-started<2 or not capture.exists():continue
            try:health=json.loads((output/'health.json').read_text())
            except (OSError,ValueError):continue
            loc=health['locomotion']
            if health['pid']!=process.pid or loc.get('animation_group')!=name or not loc.get('motion_matching'):continue
            results.append(dict(stage=stage,**loc));print(json.dumps(results[-1]),flush=True)
            stage+=1
            if stage==len(stages):
                (output/'motion-result.json').write_text(json.dumps(dict(status='complete',stages=results),indent=2))
                return
            started=now
        raise TimeoutError(f'Motion stage {stage}: {health}')
    finally:
        udp.close();process.terminate()
        try:process.wait(20)
        except subprocess.TimeoutExpired:process.kill();process.wait()
if __name__=='__main__':main()
