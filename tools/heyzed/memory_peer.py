"""Test-only Python driver around the production Java shared-memory ring implementation."""
import json,os,queue,subprocess,threading,zlib,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

class MemoryPeer:
    def __init__(self):
        java=Path('C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid/jre64/bin/java.exe')
        game=java.parents[2];build=ROOT/'.build/heyzed/memory-probe';build.mkdir(parents=True,exist_ok=True)
        source=ROOT/'tools/heyzed/adapter/Contents/mods/HeyZed/42.20.4/java/src/com/pavelvoronin/heyzed/UnrealMemory.java'
        subprocess.run([str(java),'-jar',str(Path(os.environ['TEMP'])/'PZModBuild/ecj-3.44.0.jar'),'-25','-proc:none','-cp',str(game/'projectzomboid.jar'),'-d',str(build),str(source),str(ROOT/'tools/heyzed/tests/MemoryProbe.java')],check=True)
        self.process=subprocess.Popen([str(java),'-cp',str(build)+';'+str(game/'projectzomboid.jar'),'com.pavelvoronin.heyzed.MemoryProbe',str(Path.home()/'.heyzed/bridge')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
        self.packets=queue.Queue();self.timeout=.025
        def read():
            for line in self.process.stdout:
                row=json.loads(line)
                if row['channel']=='motion':packet=row['payload'].encode()
                else:
                    raw=json.dumps(row['payload']).encode();packet=b'PJG1'+struct.pack('>I',len(raw))+zlib.compress(raw)
                self.packets.put(packet)
        self.thread=threading.Thread(target=read,daemon=True);self.thread.start()
    def settimeout(self,value):self.timeout=value
    def sendto(self,data,peer):
        if self.process.poll() is not None:raise RuntimeError('Shared-memory probe exited')
        self.process.stdin.write(data.decode()+'\n');self.process.stdin.flush()
    def recvfrom(self,size):
        import socket
        try:return self.packets.get(timeout=self.timeout),('127.0.0.1',28761)
        except queue.Empty:raise socket.timeout()
    def close(self):
        self.process.stdin.close()
        try:self.process.wait(10)
        except subprocess.TimeoutExpired:self.process.terminate();self.process.wait(10)
