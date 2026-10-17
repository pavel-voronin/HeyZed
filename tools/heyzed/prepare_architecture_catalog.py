"""Resolve complete constructions, attachments and unique ownership for the map."""
from collections import Counter,defaultdict
from pathlib import Path
import gzip
import hashlib
import json
import sqlite3
import sys

from architecture_catalog import ROOT,OUTPUT,NativeConstructionSource,encoded,publish,native_bindings
from geometry.constructions import prepare

DEST=OUTPUT/'resolved'

def request_type(request,resolved,known_types):
    if request['kind']=='garage':
        return 'door.garage.'+str(request['cells'])
    if request['kind']=='double-door' and request.get('cells',1)>1 and request.get('aperture'):
        identity='door.'+request['aperture']['id']
        if identity not in known_types:
            raise ValueError('Missing whole-assembly construction type: '+identity)
        return identity
    return resolved.get('type')



def covering_mounts(facts,tiles,bindings):
    by_name=defaultdict(list)
    for tile in tiles:
        binding=bindings.get(tile[3])
        if binding and binding['family']['category']=='covering':by_name[tile[3]].append((tile,binding))
    changed=0
    for cell in facts['cells']:
        x,y,z=cell['address']
        for wall in cell['walls']:
            axis=wall['axis'];span=wall.get('assemblyCells',1)
            for attachment in wall.get('attachments',[]):
                candidates=[]
                for name in attachment['sources']:
                    for tile,binding in by_name.get(name,[]):
                        tx,ty,tz=tile[:3];direction=binding['direction']
                        hx,hy=tx+(direction=='E'),ty+(direction=='S')
                        edge='W' if direction in ('W','E') else 'N'
                        if tz==z and edge==axis and (hy==y and x<=hx<x+span if axis=='N' else hx==x and y<=hy<y+span):candidates.append(list(tile[:3]))
                if not candidates:continue
                normals={1 if (m[1]>=y if axis=='N' else m[0]<x) else -1 for m in candidates}
                if len(normals)!=1:raise ValueError('Ambiguous native covering mount at '+str(cell['address']))
                mount=min(candidates)
                before=encoded(attachment)
                attachment.setdefault('prototypeNormal',1 if attachment.get('side')=='outside' else -1)
                attachment['mountCell']=mount
                attachment['edgeSide']='A' if (mount[1]>=y if axis=='N' else mount[0]>=x) else 'B'
                attachment.pop('side',None)
                if encoded(attachment)!=before:changed+=1
    return changed


def migrate_covering_mounts(source):
    originals={entry['id']:entry for entry in json.loads((OUTPUT/'inventory.json').read_text('utf-8'))['buildings']}
    inventory=json.loads((DEST/'inventory.json').read_text('utf-8'));count=buildings=0
    for entry in inventory['buildings']:
        path=Path(entry['file']);facts=json.loads(gzip.decompress(path.read_bytes()))
        if not any(any(a['sources'][0] in source.attachments and source.attachments[a['sources'][0]]['family']['category']=='covering' for a in w.get('attachments',[])) for c in facts['cells'] for w in c['walls']):continue
        original=json.loads(gzip.decompress(Path(originals[entry['id']]['file']).read_bytes()))
        changes=covering_mounts(facts,original['nativeTiles'],source.attachments)
        if changes:publish(path,facts);count+=changes;buildings+=1
    print(json.dumps(dict(updated_buildings=buildings,covering_mounts=count)),flush=True)


def main():
    if sys.argv[1:]==["--attachment-mounts-only"]:
        from types import SimpleNamespace
        migrate_covering_mounts(SimpleNamespace(attachments=native_bindings()));return
    source=NativeConstructionSource()
    known_types={entry['id'] for entry in source.workbench['registry']['types']}
    parents={}
    for name,properties in source.properties.items():
        sheet,_,number=name.rpartition('_')
        if number.isdigit():
            for key in ('OpenTileOffset','SmashedTileOffset','GlassRemovedOffset'):
                if key in properties:parents[sheet+'_'+str(int(number)+int(properties[key]))]=name
    inventory=json.loads((OUTPUT/'inventory.json').read_text('utf-8'))
    if not inventory['complete']:raise RuntimeError('Native inventory is still being prepared')
    revision=hashlib.sha256(Path(__file__).read_bytes()+inventory['revision'].encode()).hexdigest()
    DEST.mkdir(parents=True,exist_ok=True)
    ownership=sqlite3.connect(DEST/'ownership.sqlite')
    ownership.execute('PRAGMA journal_mode=OFF');ownership.execute('PRAGMA synchronous=OFF')
    ownership.execute('DROP TABLE IF EXISTS owners')
    ownership.execute('CREATE TABLE owners(address TEXT PRIMARY KEY,building TEXT NOT NULL) WITHOUT ROWID')
    entries=[];regions=defaultdict(list);variants={};materials=Counter();types=Counter();attachments=Counter()
    ground=defaultdict(set)
    for index,entry in enumerate(inventory['buildings']):
        snapshot=json.loads(gzip.decompress(Path(entry['file']).read_bytes()))
        cells={tuple(c['address']):c for c in snapshot['cells']}
        names={tile[3] for tile in snapshot['nativeTiles']}
        names.update(parents[name] for name in tuple(names) if name in parents)
        properties={name:source.properties[name] for name in names if name in source.properties}
        requests=prepare(snapshot['nativeTiles'],properties,set(map(tuple,snapshot['footprint'])),snapshot['metric'])['requests']
        for request in requests:
            reference=request['reference']['name']
            resolved=source.sources.get(reference,{})
            type_id=request_type(request,resolved,known_types)
            if not type_id:continue
            x,y,z=request['anchor'];axis=request['side'];key=(x,y,z)
            cell=cells.setdefault(key,dict(address=list(key),floor=None,walls=[],grime=[]))
            wall=next((w for w in cell['walls'] if w['axis']==axis),None)
            if wall is None:
                wall=dict(axis=axis,sources=request['sources'],wall=request['wall'],door=False,window=False,opening='')
                cell['walls'].append(wall)
            wall.update(insert=type_id if request['insert'] else '',openingConfig=dict(type=type_id),
                        state=request['state'],mirrorU=request.get('mirrorU',resolved.get('nativeEvidence',{}).get('mirrorU',False)),
                        fixedState=request.get('fixedState','closed'),assemblyCells=request.get('cells',1),attachments=[])
            wall['opening']='window' if request['kind']=='window' else 'door'
            for offset in range(1,request.get('cells',1)):
                other=(x+(offset if axis=='N' else 0),y+(offset if axis=='W' else 0),z)
                other_cell=cells.get(other)
                if other_cell:
                    for part in other_cell['walls']:
                        if part['axis']==axis:
                            part.update(insert='',opening='',assemblyContinuation=True,openingConfig=dict(type=''),assemblyAnchor=list(key))
            for attachment in request.get('attachments',[]):
                options={k:v for k,v in attachment.items() if k not in ('family','sources','lod')}
                spec=dict(type=type_id,family=attachment['family']['id'],options=options)
                variant=hashlib.sha256(encoded(spec)).hexdigest()[:24]
                variants[variant]=spec
                wall['attachments'].append(dict(family=attachment['family']['id'],definition='attachment.variant.'+variant,
                                                state='open' if attachment.get('open') else 'closed',side=attachment['side'],sources=attachment['sources']))
                attachments[attachment['family']['id']]+=1
        for address,cell in cells.items():
            address_text=','.join(map(str,address))
            if cell.get('floor'):
                token=address_text+':floor'
                added=ownership.execute('INSERT OR IGNORE INTO owners VALUES (?,?)',(token,entry['id'])).rowcount
                if not added:cell['floor']=None
                else:
                    materials[source.book['tiles'].get(cell['floor'],cell['floor'])]+=1
                    if address[2]==0:ground[(address[0]//256,address[1]//256)].add((address[0]%256,address[1]%256))
            for wall in cell['walls']:
                token=address_text+':'+wall['axis']
                if not ownership.execute('INSERT OR IGNORE INTO owners VALUES (?,?)',(token,entry['id'])).rowcount:wall['contextOnly']=True
                for face in ('surfaceA','surfaceB'):
                    if wall.get(face):materials[wall[face]]+=1
                if wall.get('insert'):types[wall['insert']]+=1
        snapshot['cells']=[cells[k] for k in sorted(cells)]
        snapshot['sourceRevision']=revision
        covering_mounts(snapshot,snapshot['nativeTiles'],source.attachments)
        snapshot.pop('nativeTiles',None)
        target=DEST/'buildings'/(entry['id']+'.json.gz');publish(target,snapshot)
        item=dict(entry,file=str(target.resolve()),revision=revision)
        entries.append(item)
        x0,y0,x1,y1=snapshot['bounds']
        for rx in range((x0-1)//256,(x1+1)//256+1):
            for ry in range((y0-1)//256,(y1+1)//256+1):regions[f'{rx},{ry}'].append(entry['id'])
        if (index+1)%100==0:
            ownership.commit();print(json.dumps(dict(buildings=index+1,total=len(inventory['buildings']),variants=len(variants))),flush=True)
    ownership.commit();ownership.close()
    for (rx,ry),positions in ground.items():
        mask=bytearray(256*256//8)
        for x,y in positions:
            bit=y*256+x;mask[bit//8]|=1<<(bit%8)
        path=DEST/'floor-masks'/f'{rx}_{ry}.bin';path.parent.mkdir(exist_ok=True);path.write_bytes(mask)
    publish(OUTPUT/'attachment-variants.json',variants)
    publish(DEST/'inventory.json',dict(schema=2,complete=True,revision=revision,buildings=entries,regions=regions,
                                      materialUsage=dict(materials),typeUsage=dict(types),attachmentUsage=dict(attachments),
                                      floorMaskDirectory=str((DEST/'floor-masks').resolve())))
    print(json.dumps(dict(complete=True,buildings=len(entries),variants=len(variants),types=len(types),materials=len(materials))),flush=True)

if __name__=='__main__':main()
