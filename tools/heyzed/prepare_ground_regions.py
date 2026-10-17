"""Compile native floor facts into regional recipes using the authored XY catalogue.

Reads only the existing native PZBF floor facts; never reads former contour resources.
"""
from array import array
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import zlib
import ground_geometry

ROOT=Path(__file__).resolve().parent
TILES={}
MESHES={}


def initialize(catalog):
    global TILES,MESHES
    value=json.loads(Path(catalog).read_text(encoding='utf-8'))
    TILES=value['tiles'];MESHES=value['meshes']


def convert(job):
    source,destination=map(Path,job)
    raw=zlib.decompress(source.read_bytes())
    magic,version,x,y,length=struct.unpack_from('>4siiiI',raw)
    if magic!=b'PZBF' or version!=1: raise ValueError('Unsupported native source facts')
    layers=json.loads(raw[20:20+length]);grid=array('H');grid.frombytes(raw[20+length:])
    if sys.byteorder=='little':grid.byteswap()
    if len(grid)!=65536:raise ValueError('Invalid native source grid')
    styles=[None];ids={};mapping=[0]*len(layers);unresolved=set();changed=False
    square=[[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]]
    for i,stack in enumerate(layers):
        base=next((t['material'] for t in stack if not t['overlay']),None)
        if base is None:continue
        base='Grass' if base.startswith('Grass') else base or 'Unknown'
        if base not in ground_geometry.BASE_MATERIALS:base='Unknown'
        parts=[]
        for tile in stack:
            if tile['name'] in TILES:parts.extend(TILES[tile['name']])
            elif not tile['overlay']:parts.append('GroundBase_'+base)
            else:unresolved.add(tile['name'])
        changed |= any(t['overlay'] and bool(TILES.get(t['name'])) for t in stack)
        uniform=len(parts)==1 and (parts[0]=='GroundBase_'+base or MESHES[parts[0]]['outline']==[[0,0],[1,0],[1,1],[0,1]])
        style=dict(base=base,parts=parts,uniform=uniform)
        key=json.dumps(style,separators=(',',':'))
        if key not in ids:ids[key]=len(styles);styles.append(style)
        mapping[i]=ids[key]
    rendered=array('H',(mapping[i] for i in grid))
    if len(styles)>65536:raise ValueError('Regional ground style capacity exceeded')
    if sys.byteorder=='big':rendered.byteswap()
    target=destination/(source.stem+'.json')
    target.write_text(json.dumps(dict(schema=2,x=x,y=y,styles=styles,unresolved=sorted(unresolved),changed=changed),separators=(',',':')),encoding='utf-8')
    target.with_suffix('.grid').write_bytes(zlib.compress(rendered.tobytes(),6))
    return source.stem if changed else None


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--workers',type=int,default=4);args=parser.parse_args()
    native=json.loads((ROOT/'.local/world/current.json').read_text(encoding='utf-8'))
    catalog=ground_geometry.compile_catalog()
    encoded=json.dumps(catalog,separators=(',',':')).encode()
    digest=hashlib.sha256(encoded+Path(__file__).read_bytes()+native['identity'].encode()).hexdigest()
    destination=ROOT/'.local/authored-regions'/digest;destination.mkdir(parents=True,exist_ok=True)
    index=destination/'catalog.json';index.write_bytes(encoded)
    sources=sorted(Path(native['folder']).glob('*.pzf'))
    affected=[]
    with ProcessPoolExecutor(max_workers=args.workers,initializer=initialize,initargs=(str(index),)) as pool:
        for i,key in enumerate(pool.map(convert,((str(p),str(destination)) for p in sources),chunksize=8),1):
            if key:affected.append(key)
            if i%200==0:print(f'Prepared authored ground regions: {i}/{len(sources)}',flush=True)
    manifest=dict(recipe=digest,identity=native['identity'],folder=str(destination),regions=affected,source_regions=len(sources))
    (ROOT/'.local/authored-regions/current.json').write_text(json.dumps(manifest,separators=(',',':')),encoding='utf-8')
    print(f'Prepared {len(sources)} native ground regions',flush=True)


if __name__=='__main__':main()
