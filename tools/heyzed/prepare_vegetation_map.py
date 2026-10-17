"""Prepare addressed baseline vegetation facts without rebuilding the ground world."""
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import hashlib
import json
import os
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'roof-lab'))
from pzdata import header

PROPS={}


def initialize():
    global PROPS
    catalog=json.loads((ROOT.parent/'resources-lab/.local/catalog.json').read_text())
    for row in catalog['rows']:
        if row['kind']!='tile':continue
        name=row['name'];p=row.get('properties',{})
        if 'tree' in p:
            kind=2 if any(family in name.lower() for family in ['pine','hemlock','cedar']) else 0
            PROPS[name]=(kind,max(1,min(8,int(p['tree']))),name,0)
        elif 'Bush' in p or name.startswith(('e_newbushes','f_bushes_')) or p.get('Movement','').startswith('Hedge'):
            PROPS[name]=(1,1,name,0)


def prepare(job):
    source,folder,recipe=job
    path=Path(folder)/(Path(source[0]).stem+'.json')
    stamp=hashlib.sha256((str(source)+recipe).encode()).hexdigest()
    if path.exists():
        old=json.loads(path.read_text())
        if old.get('stamp')==stamp and path.with_suffix('.pzvg').exists():return old['plants']
    h=header(Path(source[0]));definitions=[PROPS.get(name) for name in h['names']]
    cells=[];plants=0
    if any(definitions):
        data=Path(source[3]).read_bytes();table=12 if data[:4]==b'LOTP' else 4
        unpack=struct.Struct('<i').unpack_from
        for cx in range(32):
            for cy in range(32):
                pos=unpack(data,table+(cx*32+cy)*8)[0]
                if pos<0:continue
                cell=0;last=(1-h['low'])*64
                while cell<last:
                    count=unpack(data,pos)[0];pos+=4
                    if count==-1:
                        skip=unpack(data,pos)[0];pos+=4
                        if skip<=0:raise ValueError('Invalid native map empty run')
                        cell+=skip;continue
                    if count>1:
                        if count>4096:raise ValueError('Invalid native map stack')
                        pos+=4
                        ids=struct.unpack_from('<'+str(count-1)+'i',data,pos);pos+=(count-1)*4
                        if cell>=-h['low']*64:
                            found=[definitions[i] for i in ids if definitions[i]]
                            if found:
                                xy=cell%64;x=h['cx']*256+cx*8+xy//8;y=h['cy']*256+cy*8+xy%8
                                cells.append([x,y,found]);plants+=len(found)
                    cell+=1
    raw=bytearray(struct.pack('<4sII',b'PZVG',1,plants))
    for x,y,found in cells:
        for kind,stage,name,z in found:raw.extend(struct.pack('<BBBBI',x%256,y%256,kind,stage,zlib.crc32(name.encode())))
    binary=path.with_suffix('.pzvg');pending=binary.with_suffix('.pending');pending.write_bytes(raw);os.replace(pending,binary)
    out=dict(stamp=stamp,plants=plants)
    pending=path.with_suffix('.pending');pending.write_text(json.dumps(out,separators=(',',':')));os.replace(pending,path)
    return plants


if __name__=='__main__':
    manifest=json.loads((ROOT/'.local/world/current.json').read_text())
    folder=ROOT/'client/Saved/HeyZed/Vegetation'/manifest['identity'];folder.mkdir(parents=True,exist_ok=True)
    recipe=hashlib.sha256(Path(__file__).read_bytes()+(ROOT.parent/'resources-lab/.local/catalog.json').read_bytes()).hexdigest()
    total=0
    with ProcessPoolExecutor(max_workers=4,initializer=initialize) as pool:
        for i,count in enumerate(pool.map(prepare,[(s,str(folder),recipe) for s in manifest['source_files']]),1):
            total+=count
            if i%128==0:print(f'Prepared vegetation regions {i}/{len(manifest["source_files"])}; plants {total}',flush=True)
    print(f'Native vegetation baseline ready: {len(manifest["source_files"])} regions, {total} plants',flush=True)
