"""Prepare immutable outdoor fixture placements from native map stacks."""
from concurrent.futures import ProcessPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'roof-lab'))
from pzdata import header,read_chunk

PROPS={}
def initialize(catalog):
    global PROPS
    PROPS={r['name']:r.get('properties',{}) for r in json.loads(Path(catalog).read_text())['rows']
           if r['kind']=='tile' and 'streetlight' in r.get('properties',{}) and 'lightswitch' in r.get('properties',{}) and 'lightR' in r.get('properties',{})}

def prepare(job):
    header_name,pack_name,folder,stamp=job
    out=Path(folder)/(Path(header_name).stem+'.json')
    if out.exists():
        old=json.loads(out.read_text())
        if old['stamp']==stamp:return old
    h=header(Path(header_name));ids={i for i,name in enumerate(h['names']) if name in PROPS}
    fixtures=[]
    if ids:
        pack=Path(pack_name).read_bytes()
        # Scan only chunks that exist; preserve native height and stack ownership.
        for cx in range(32):
            for cy in range(32):
                stacks=read_chunk(pack,h,h['cx']*32+cx,h['cy']*32+cy,ids)
                for x,y,z,room,names in stacks:
                    if z not in (0,1):continue
                    for name in names:
                        p=PROPS.get(name)
                        if p is None:continue
                        base=z
                        if z==1:
                            lower=read_chunk(pack,h,x//8,y//8)
                            paired=any(a==x and b==y and level==0 and any(n in ('lighting_outdoor_01_4','lighting_outdoor_01_16','lighting_outdoor_01_17') for n in ns) for a,b,level,_,ns in lower)
                            if not paired:continue
                            base=0
                        fixtures.append(dict(id=f'{x}:{y}:{z}:{name.replace("_on_","_")}',position=[(x+.5)*100,(y+.5)*100,base*300],
                            radius=min(20,max(1,int(p.get('LightRadius',10)) or 10))*100,
                            color=[min(1,float(p.get(c,255))/255*2) for c in ('lightR','lightG','lightB')]))
    poles={}
    for fixture in fixtures:
        key=tuple(fixture['position'])
        if key in poles:
            poles[key]['controls'].append(fixture['id'])
            poles[key]['radius']=max(poles[key]['radius'],fixture['radius'])
        else:
            fixture['controls']=[fixture['id']];poles[key]=fixture
    result=dict(stamp=stamp,x=h['cx'],y=h['cy'],fixtures=list(poles.values()))
    pending=out.with_suffix('.pending');pending.write_text(json.dumps(result,separators=(',',':')));pending.replace(out)
    return result

def main():
    base=json.loads((ROOT/'.local/world/current.json').read_text())
    catalog=ROOT.parent/'resources-lab/.local/catalog.json'
    recipe=hashlib.sha256(Path(__file__).read_bytes()+catalog.read_bytes()+base['identity'].encode()).hexdigest()
    folder=ROOT/'.local/lighting'/recipe;folder.mkdir(parents=True,exist_ok=True)
    jobs=[(r[0],r[3],str(folder),r) for r in base['source_files']]
    regions=[]
    with ProcessPoolExecutor(max_workers=4,initializer=initialize,initargs=(str(catalog),)) as pool:
        for i,row in enumerate(pool.map(prepare,jobs,chunksize=4),1):
            if row['fixtures']:regions.append(dict(x=row['x'],y=row['y'],fixtures=row['fixtures']))
            if i%500==0:print(f'Prepared fixture placements {i}/{len(jobs)}',flush=True)
    target=ROOT/'.local/lighting/current.json';pending=target.with_suffix('.pending')
    pending.write_text(json.dumps(dict(identity=base['identity'],recipe=recipe,regions=regions),separators=(',',':')));pending.replace(target)
    print(f'Prepared {sum(len(r["fixtures"]) for r in regions)} outdoor fixtures in {len(regions)} regions',flush=True)

if __name__=='__main__':main()
