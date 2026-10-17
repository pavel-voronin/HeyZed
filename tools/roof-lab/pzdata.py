"""Read-only B42 map, tile and image readers. Coordinates are native, not guessed."""
from pathlib import Path
from functools import lru_cache
import io
import json
import re
import struct
from PIL import Image

SCHEMA = 1
FLOOR = 2.449489742783178
I = struct.Struct('<i')

def number(f):
    return I.unpack(f.read(4))[0]

def line(f):
    value = f.readline(4097)
    if not value.endswith(b'\n'):
        raise ValueError('Truncated or oversized map string')
    return value.decode('utf-8').strip()

def header(path):
    cx, cy = map(int, path.stem.split('_'))
    with path.open('rb') as f:
        magic = f.read(4)
        version = number(f) if magic == b'LOTH' else I.unpack(magic)[0]
        if version not in (0, 1): raise ValueError('Unsupported lotheader version')
        count = number(f)
        if not 0 <= count <= 1000000: raise ValueError('Invalid tile count')
        names = [line(f) for _ in range(count)]
        if version == 0: f.read(1)
        if (number(f), number(f)) != (8, 8): raise ValueError('Expected B42 8x8 chunks')
        low = 0 if version == 0 else number(f)
        high = number(f) - (version == 0)
        rooms = []
        for _ in range(number(f)):
            name, z, n = line(f), number(f), number(f)
            rects = []
            for _ in range(n):
                x, y, w, h = (number(f) for _ in range(4))
                rects.append([x+cx*256, y+cy*256, w, h])
            objects = [struct.unpack('<3i', f.read(12)) for _ in range(number(f))]
            rooms.append(dict(name=name, z=z, rects=rects, objects=objects))
        buildings = []
        for index in range(number(f)):
            rs = [rooms[number(f)] for _ in range(number(f))]
            inside = [r for r in rs if r['name'] != 'emptyoutside']
            rects = [q for r in inside for q in r['rects']]
            if not rects: rects=[q for r in rs for q in r['rects']]
            if not rects: continue
            bounds = [min(q[0] for q in rects), min(q[1] for q in rects),
                      max(q[0]+q[2] for q in rects), max(q[1]+q[3] for q in rects)]
            buildings.append(dict(id=f'{cx}_{cy}_{index}', bounds=bounds, rooms=rs,
                                  label=', '.join(sorted({r['name'] for r in inside})[:4]) or 'Outdoor construction'))
    return dict(names=names, low=low, high=high, buildings=buildings, rooms=rooms, cx=cx, cy=cy)

def read_chunk(pack, h, wx, wy, tile_ids=None):
    """Preserve ordered stacks and rooms; optional palette IDs select whole stacks."""
    table = 12 if pack[:4] == b'LOTP' else 4
    offset = I.unpack_from(pack, table+((wx % 32)*32+wy % 32)*8)[0]
    if offset < 0: return []
    if not table+8192 <= offset < len(pack): raise ValueError('Invalid lotpack offset')
    result, cell = [], 0
    total = (h['high']-h['low']+1)*64
    while cell < total:
        count = I.unpack_from(pack, offset)[0]; offset += 4
        if count == -1:
            skip = I.unpack_from(pack, offset)[0]; offset += 4
            if not 0 < skip <= total-cell: raise ValueError('Invalid empty run')
            cell += skip; continue
        if count > 1:
            if count > 4096: raise ValueError('Invalid tile stack')
            room = I.unpack_from(pack, offset)[0]; offset += 4
            ids = struct.unpack_from(f'<{count-1}i', pack, offset); offset += (count-1)*4
            if tile_ids is None or not tile_ids.isdisjoint(ids):
                z, xy = divmod(cell, 64)
                result.append((wx*8+xy//8, wy*8+xy%8, h['low']+z, room, [h['names'][i] for i in ids]))
        cell += 1
    return result

def definitions(media):
    result = {}
    for file in sorted(media.glob('*.tiles.txt')):
        for match in re.finditer(r'//\s*(\S+)\s*\n\s*tile\s*\{([^}]+)\}', file.read_text('utf-8-sig')):
            values = {a.strip(): b.strip() for a,b in re.findall(r'^\s*(\w+)\s*=([^\n]*)', match[2], re.M)}
            if '.patch.' in file.name:
                result.setdefault(match[1], {}).update(values)
            else:
                result[match[1]] = values
    return result

def kind(name, props):
    if name.startswith('walls_') and '_roofs_' in name: return 'gable'
    if name.startswith(('rooftop_', 'roofs_accents_')) or 'chimney' in name.lower() or 'isEave' in props: return 'detail'
    if name.startswith(('roofs_', 'roof_')) or any(k in props for k in ('WestRoofT','WestRoofM','WestRoofB')): return 'slope'
    if 'solidfloor' in props or name.startswith(('floors_', 'roofs_flat_', 'ceilings_')): return 'floor'
    if 'WallN' in props or 'WallW' in props or 'wall' in props: return 'wall'
    return None

def blocks(text):
    """PZ ScriptParser's relevant block subset; preserve all primitive types."""
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    root = dict(type='root', props={}, children=[])
    stack, pending = [root], ''
    for part in re.split(r'([{}])', text):
        if part == '{':
            child = dict(type=pending.strip().strip(','), props={}, children=[])
            stack[-1]['children'].append(child); stack.append(child); pending=''
        elif part == '}':
            _properties(stack[-1], pending); pending=''; stack.pop()
        else:
            # Text before a nested brace contains assignments and the next type.
            lines = part.strip().splitlines()
            for v in lines:
                if '=' in v: _properties(stack[-1], v)
                elif v.strip().strip(','): pending=v.strip().strip(',')
    if len(stack) != 1: raise ValueError('Unbalanced script blocks')
    return root

def _properties(block, value):
    if '=' in value:
        k,v=value.split('=',1); block['props'][k.strip()]=v.strip().rstrip(',')

def geometry(media):
    data = {}
    tree = blocks((media/'tileGeometry.txt').read_text('utf-8-sig'))
    for sheet in tree['children'][0]['children']:
        for tile in sheet['children']:
            if tile['type'] != 'tile': continue
            col,row=map(int,tile['props']['xy'].split('x'))
            name=f"{sheet['props']['name']}_{row*8+col}"
            shapes=[]; props={}
            for child in tile['children']:
                if child['type']=='properties': props.update(child['props']); continue
                shape=dict(type=child['type'])
                for k,v in child['props'].items():
                    if k in ('translate','rotate','min','max'):
                        shape[k]=[int(n)/10000 for n in v.split('x')]
                    elif k=='points': shape[k]=[[int(n)/10000 for n in p.split('x')] for p in v.split()]
                    else: shape[k]=v
                shapes.append(shape)
            data[name]=dict(shapes=shapes, properties=props)
    return data

def assignments(media):
    return dict(re.findall(r'^\s*(\w+)\s*=\s*(\w+)\s*,', (media/'tileDepthTextureAssignments.txt').read_text(), re.M))

def seams(media):
    result={}
    for sheet in blocks((media/'seams.txt').read_text())['children'][0]['children']:
        for tile in sheet['children']:
            col,row=map(int,tile['props']['xy'].split('x'))
            result[f"{sheet['props']['name']}_{row*8+col}"]={c['type']:c['props'] for c in tile['children']}
    return result

class Resources:
    def __init__(self, game, local):
        self.game=Path(game); self.media=self.game/'media'; self.local=Path(local)
        self.props=definitions(self.media); self.geometry=geometry(self.media)
        self.assignments=assignments(self.media); self.seams=seams(self.media)
        self.textures=self._texture_index()

    def source(self, name):
        # Assignment manager uses a direct alias, not arbitrary recursive aliases.
        return self.assignments.get(name, name)

    def _texture_index(self):
        packs=[self.media/'texturepacks'/n for n in ('Tiles1x.pack','Tiles1x.floor.pack','Tiles2x.pack','Tiles2x.floor.pack','B42ChunkCaching2x.pack','B42ChunkCaching2x.floor.pack','Overlays2x.pack','Overlays2x.floor.pack','JumboTrees2x.pack','JumboTreesBigs2x.pack')]
        stamp=[(str(p),p.stat().st_size,p.stat().st_mtime_ns) for p in packs if p.exists()]
        cache=self.local/'texture-index.json'
        if cache.exists():
            old=json.loads(cache.read_text())
            if old['stamp']==[list(s) for s in stamp]: return old['textures']
        textures={}
        for path,_,_ in stamp:
            with open(path,'rb') as f:
                modern=f.read(4)==b'PZPK'
                if modern:
                    if number(f)!=1:raise ValueError('Unsupported pack')
                else:f.seek(0)
                def string():
                    n=number(f)
                    if not 0<=n<=4096: raise ValueError('Bad texture name')
                    return f.read(n).decode()
                for _ in range(number(f)):
                    string(); count=number(f); number(f); entries=[]
                    for _ in range(count):
                        name=string(); entries.append((name,struct.unpack('<8i',f.read(32))))
                    if modern:
                        length=number(f); offset=f.tell(); f.seek(length,1)
                    else:
                        offset=f.tell()
                        if f.read(8)!=b'\x89PNG\r\n\x1a\n':raise ValueError('Invalid legacy texture page')
                        while True:
                            chunk=f.read(8)
                            if len(chunk)!=8:raise ValueError('Truncated legacy texture page')
                            f.seek(struct.unpack('>I',chunk[:4])[0]+4,1)
                            if chunk[4:]==b'IEND':break
                        length=f.tell()-offset
                        if f.read(4)!=b'\xef\xbe\xad\xde':raise ValueError('Invalid legacy page terminator')
                    for name, rect in entries: textures[name]=[path,offset,length,list(rect)]
        self.local.mkdir(parents=True,exist_ok=True)
        cache.write_text(json.dumps(dict(stamp=stamp,textures=textures)))
        return textures

    @lru_cache(maxsize=8)
    def page(self, path, offset, length):
        with open(path,'rb') as f: f.seek(offset); raw=f.read(length)
        return Image.open(io.BytesIO(raw)).convert('RGBA')

    @lru_cache(maxsize=512)
    def image(self, name):
        meta=self.textures.get(name)
        if meta is None: return None
        path,off,length,(x,y,w,h,ox,oy,ow,oh)=meta
        result=Image.new('RGBA',(ow,oh))
        result.paste(self.page(path,off,length).crop((x,y,x+w,y+h)),(ox,oy))
        if '1x' in Path(path).name:result=result.resize((ow*2,oh*2),Image.Resampling.NEAREST)
        return result

    @lru_cache(maxsize=32)
    def depth_sheet(self, sheet):
        p=self.media/'depthmaps'/f'DEPTH_{sheet}.png'
        return Image.open(p).convert('RGBA') if p.exists() else None

    def depth(self, name):
        source=self.source(name); sheet,index=source.rsplit('_',1)
        atlas=self.depth_sheet(sheet)
        if atlas is None: return None
        i=int(index); x=(i%8)*128; y=(i//8)*256
        return atlas.crop((x,y,x+128,y+256)) if y+256<=atlas.height else None
