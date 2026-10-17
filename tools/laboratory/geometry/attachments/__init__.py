"""Accepted attachment catalog and native edge bindings for every laboratory."""
import json
from pathlib import Path

SOURCE = Path(__file__).with_name('families.json')

def catalog():
    return json.loads(SOURCE.read_text('utf-8'))

def families():
    return catalog()['families']

def native_rules():
    return catalog()['nativeRules']

def native_bindings():
    result = {}
    for family in families():
        if family['category'] == 'covering':
            prefix, number = family['nativeBase'].rsplit('_', 1)
            for offset, direction in enumerate(('W', 'E', 'N', 'S')):
                for opened in (False, True):
                    result[f'{prefix}_{int(number)+offset+4*opened}'] = dict(family=family, direction=direction, open=opened)
        elif family['category'] == 'decor':
            prefix, number = family['references']['closed'][0]['name'].rsplit('_', 1)
            for offset in range(4):
                result[f'{prefix}_{int(number)+offset}'] = dict(family=family, direction='W' if offset < 2 else 'N', panel=(offset % 2) ^ int(offset >= 2))
        elif family['id'] == 'wood':
            for direction, base in (('W',8),('N',9),('E',0),('S',1)):
                for count in range(1,5):
                    result[f'carpentry_01_{base+(count-1)*2}'] = dict(family=family,direction=direction,planks=count)
        elif family['id'] == 'metal-sheet':
            for direction, base in (('W',24),('N',25),('E',28),('S',29)):
                for damage in (0,2):
                    result[f'constructedobjects_01_{base+damage}'] = dict(family=family,direction=direction,damaged=bool(damage))
        elif family['id'] == 'metal-bars':
            for direction, number in (('W',55),('N',53),('E',52),('S',54)):
                result[f'constructedobjects_01_{number}'] = dict(family=family,direction=direction)
    return result

def bind(tiles, requests, consumed, properties=None):
    """Resolve native occupied edges; attachments never derive metrics from pixels."""
    bindings = native_bindings()
    edges = {}
    for request in requests:
        x,y,z = request['anchor']
        for step in range(request.get('cells',1)):
            edges[(x+(step if request['side']=='N' else 0),y+(step if request['side']=='W' else 0),z,request['side'])] = request

    def host(tile, direction):
        x,y,z = tile[:3]
        return edges.get((x+(direction=='E'),y+(direction=='S'),z,'W' if direction in ('W','E') else 'N'))

    def attach(request, binding, indices, sign):
        item = {k:v for k,v in binding.items() if k not in ('direction','panel')}
        item.update(side='outside' if sign>0 else 'inside', depthSign=sign, lod='medium', sources=[tiles[i][3] for i in indices])
        existing=next((a for a in request.get('attachments',[]) if a['family']['id']==item['family']['id'] and a['side']==item['side']),None) if item['family']['category']=='decor' else None
        if existing:
            existing['sources'].extend(item['sources'])
            existing['panels']=sorted(set(existing['panels'])|set(item['panels']))
        else:request.setdefault('attachments',[]).append(item)
        request['sources'].extend(item['sources'])
        consumed.update(indices)

    for i,tile in enumerate(tiles):
        if i in consumed or tile[3] not in bindings or properties is not None and tile[3] not in properties:continue
        binding=bindings[tile[3]];family=binding['family'];direction=binding['direction'];request=host(tile,direction)
        neighbor=False
        if not request and family['category']=='decor':
            shifted=list(tile);axis=1 if direction=='W' else 0
            shifted[axis]+=1 if binding['panel']==1 else -1
            request=host(shifted,direction);neighbor=True
        if not request:continue
        window=request['kind']=='window'
        props=request['reference']['properties']
        if family['category']=='covering' and not window and not (family['kind']=='sheet' and 'doorTrans' in props and 'GarageDoor' not in props and props.get('DoorSound','').lower()!='slidingglassdoor'):continue
        if family['category']=='decor' and not window:continue
        if family['category']=='barricade' and (request['kind'] in ('garage','gate') or 'DoubleDoor' in props):continue
        indices=[i]
        if family['category']=='decor':
            # Both pieces are sprites on the same native edge, not separate shutters.
            if not neighbor:
                indices += [j for j,t in enumerate(tiles) if j not in consumed and j!=i and (properties is None or t[3] in properties) and t[:3]==tile[:3] and (other:=bindings.get(t[3])) and other['family']['id']==family['id'] and other['direction']==direction]
            binding=dict(binding,panels=sorted({1-bindings[tiles[j][3]]['panel'] if neighbor else bindings[tiles[j][3]]['panel'] for j in indices}))
        attach(request,binding,indices,1 if direction in ('N','W') else -1)

    rope=next(f for f in families() if f['id']=='sheet-rope')
    for i,tile in enumerate(tiles):
        if i in consumed or tile[3] not in ('crafted_01_0','crafted_01_1','crafted_01_3','crafted_01_4') or tile[2]<=0 or properties is not None and tile[3] not in properties:continue
        number=int(tile[3].rsplit('_',1)[1]);side='W' if number in (0,3) else 'N'
        request=edges.get((*tile[:3],side))
        if not request or request['kind']!='window':continue
        x,y,z=tile[:3];shifted=number in (3,4);cx=x-(shifted and side=='W');cy=y-(shifted and side=='N')
        upper=2 if side=='W' else 5;lower=10 if side=='W' else 13
        if not shifted:lower=number+8
        indices=[i];lowest=z
        for j,t in enumerate(tiles):
            if j in consumed or j==i or t[0]!=cx or t[1]!=cy or t[2]>z or properties is not None and t[3] not in properties:continue
            if t[3]==f'crafted_01_{lower}' or (shifted and t[2]==z and t[3]==f'crafted_01_{upper}'):
                indices.append(j);lowest=min(lowest,t[2])
        attach(request,dict(family=rope,floors=max(1,z-lowest)),indices,-1 if shifted else 1)
