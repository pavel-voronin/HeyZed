pass






import argparse
import collections
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import zipfile

DEFINITIONS = [
    ('newtiledefinitions.tiles', 1), ('tiledefinitions_erosion.tiles', 2),
    ('tiledefinitions_overlays.tiles', 4), ('tiledefinitions_b42chunkcaching.tiles', 5),
    ('tiledefinitions_noiseworks.patch.tiles', -1), ('jumbo_trees_big.tiles', 6),
    ('jumbo_trees.tiles', 8),
]
TILE_TOKEN = re.compile(r'(?<![\w&-])([A-Za-z][\w&-]*_\d+)(?![\w&-])')
OFFSETS = {'Noffset': 'orientation', 'Eoffset': 'orientation', 'Soffset': 'orientation',
           'Woffset': 'orientation', 'LinkedOffset': 'linked-state',
           'OpenTileOffset': 'open-state', 'SmashedTileOffset': 'smashed-state',
           'GlassRemovedOffset': 'glass-removed-state', 'CurtainOpen': 'curtain-state',
           'StackReplaceTileOffset': 'stack-height'}


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def int32(stream):
    return struct.unpack('<i', stream.read(4))[0]


def definitions(path):
    with path.open('rb') as f:
        magic = f.read(4)
        assert magic == b'tdef', (path, magic)
        assert int32(f) == 1
        result = []
        for _ in range(int32(f)):
            name = f.readline().decode('latin1').strip()
            image = f.readline().decode('latin1').strip()
            width, height, set_id, count = (int32(f) for _ in range(4))
            for index in range(count):
                properties = {}
                for _ in range(int32(f)):
                    key = f.readline().decode('latin1').strip()
                    properties[key] = f.readline().decode('latin1').strip()
                result.append(dict(name=f'{name}_{index}', tileset=name, index=index,
                                   sheetSize=[width, height], tilesetId=set_id,
                                   properties=properties))
        assert not f.read(), f'Trailing bytes in {path}'
        return result


def pack_metadata(path):
    pass
    def string(f):
        n = int32(f)
        if not 0 <= n <= 1048576:
            raise ValueError(f'Invalid string length {n}: {path}')
        return f.read(n).decode('utf-8', errors='strict')
    with path.open('rb') as f:
        magic = f.read(4)
        version = int32(f) if magic == b'PZPK' else 0
        if not version:
            f.seek(0)
        assert version in (0, 1)
        result = []
        for _ in range(int32(f)):
            page = string(f)
            count, mask = int32(f), int32(f)
            for _ in range(count):
                name = string(f)
                values = struct.unpack('<8i', f.read(32))
                result.append(dict(name=name, pack=path.name, page=page,
                                   rect=list(values[:4]), offset=list(values[4:6]),
                                   originalSize=list(values[6:]), alpha=bool(mask)))
            if version:
                length = int32(f)
                assert length >= 0
                f.seek(length, 1)
            else:
                                                                        
                tail = b''
                while True:
                    data = f.read(65536)
                    if not data:
                        raise EOFError(f'Missing image terminator: {path}')
                    combined = tail + data
                    pos = combined.find(b'\xef\xbe\xad\xde')
                    if pos >= 0:
                        f.seek(-(len(combined) - pos - 4), 1)
                        break
                    tail = combined[-3:]
        assert not f.read(), f'Trailing bytes in {path}'
        return result


def class_info(data):
    pass




    f = io.BytesIO(data)
    def u2(): return struct.unpack('>H', f.read(2))[0]
    def u4(): return struct.unpack('>I', f.read(4))[0]
    assert u4() == 0xCAFEBABE
    u2(); u2()
    cp = [None] * u2()
    i = 1
    while i < len(cp):
        tag = f.read(1)[0]
        if tag == 1:
            cp[i] = (tag, f.read(u2()).decode('utf-8', errors='replace'))
        elif tag in (3, 4): f.read(4)
        elif tag in (5, 6): f.read(8); i += 1
        elif tag in (7, 8, 16, 19, 20): cp[i] = (tag, u2())
        elif tag in (9, 10, 11, 12, 17, 18): cp[i] = (tag, u2(), u2())
        elif tag == 15: f.read(3)
        else: raise ValueError(f'Unknown constant tag {tag}')
        i += 1
    text = lambda index: cp[index][1]
    u2(); this, parent = u2(), u2()
    name = text(cp[this][1])
    superclass = text(cp[parent][1]) if parent else None
    f.read(u2() * 2)
    def attributes():
        for _ in range(u2()): u2(); f.seek(u4(), 1)
    for _ in range(u2()):
        u2(); u2(); u2(); attributes()
    methods = []
    for _ in range(u2()):
        u2(); method, desc = text(u2()), text(u2()); attributes()
        methods.append([method, desc])
    strings = [text(c[1]) for c in cp if c and c[0] == 8]
    calls = []
    for c in cp:
        if c and c[0] in (10, 11):
            owner = text(cp[c[1]][1]); nt = cp[c[2]]
            calls.append([owner, text(nt[1])])
    return dict(name=name, parent=superclass, strings=strings, methods=methods, calls=calls)


def write_jsonl(path, rows):
                                                                         
    with path.open('wb') as raw, gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=0) as gz:
        for row in rows:
            gz.write((json.dumps(row, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n').encode())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--native', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    game, media, out = args.game, args.game / 'media', args.output
    out.mkdir(parents=True, exist_ok=True)
    provenance = []
    raw_tiles, layers, definition_counts = {}, collections.defaultdict(list), {}
    for file, file_id in DEFINITIONS:
        path = media / file
        rows = definitions(path)
        definition_counts[file] = len(rows)
        provenance.append(dict(path=f'media/{file}', sha256=sha(path)))
        for r in rows:
            name = r['name']
            if file_id == -1 and name not in raw_tiles:
                continue
            layers[name].append(file)
            if file_id != -1:
                raw_tiles[name] = r
            else:
                raw_tiles[name]['properties'].update(r['properties'])
    native = {r['name']: r for r in map(json.loads, args.native.read_text(encoding='utf-8').splitlines())}
    missing = sorted(set(raw_tiles) - set(native))
    extra = sorted(set(native) - set(raw_tiles))
    assert not missing, f'Native loader missed {len(missing)} definitions: {missing[:5]}'
                                                                                   
    texture_rows, packs, pack_errors = [], {}, []
    for path in sorted((media / 'texturepacks').glob('*.pack')):
        rows = pack_metadata(path)
        texture_rows.extend(rows)
        packs[path.name] = len(rows)
        provenance.append(dict(path=f'media/texturepacks/{path.name}', sha256=sha(path)))
    textures = collections.defaultdict(set)
    for r in texture_rows: textures[r['name']].add(r['pack'])
    packed_names = frozenset(textures)
    names = set(raw_tiles) | packed_names
    prefixes = {n.rsplit('_', 1)[0] for n in names if re.fullmatch(r'.+_\d+', n)}
    references = collections.defaultdict(set)
    prefix_references = collections.defaultdict(set)
    source_records, render_classes = [], []
    def collect(text, source):
        for token in set(TILE_TOKEN.findall(text)) & names:
            references[token].add(source)
                                                                                       
        for token in set(re.findall(r'[A-Za-z][\w&-]*', text)):
            if token.rstrip('_') in prefixes:
                prefix_references[token.rstrip('_')].add(source)
    for directory in ('lua', 'scripts'):
        for path in sorted((media / directory).rglob('*')):
            if path.suffix not in ('.lua', '.txt') or 'Translate' in path.parts:
                continue
            rel = path.relative_to(game).as_posix()
            text = path.read_text(encoding='utf-8-sig', errors='replace')
            collect(text, rel)
            source_records.append(dict(path=rel, sha256=sha(path)))
    for path in sorted(media.rglob('*')):
        if not path.is_file() or path.suffix not in ('.txt','.json','.xml') or path.relative_to(media).parts[0] in ('lua','scripts'):
            continue
        rel = path.relative_to(game).as_posix()
        collect(path.read_text(encoding='utf-8-sig',errors='replace'),rel)
        source_records.append(dict(path=rel,sha256=sha(path)))
                                                                               
                                                                              
    native_tables = {}
    declared_mechanisms = collections.defaultdict(set)
    table_relations = []
    def strings(value):
        if isinstance(value, str): yield value
        elif isinstance(value, dict):
            for k,v in value.items():
                yield k
                yield from strings(v)
        elif isinstance(value, list):
            for v in value: yield from strings(v)
    for kind in ('erosion', 'sprite-configs', 'lua-tables', 'geometry'):
        path = args.native.with_name('native-' + kind + '.jsonl')
        data = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()]
        native_tables[kind] = data
        for row in data:
            source = row.get('source') or 'java:' + row['category']
            if kind == 'geometry': continue
            if kind == 'erosion': mechanism = 'erosion-growth-season'
            elif kind == 'sprite-configs': mechanism = 'scripted-sprite-assembly'
            elif '/Farming/' in source: mechanism = 'farming-growth'
            elif 'Fences' in source: mechanism = 'damage-replacement'
            elif 'WorldFiller' in source: mechanism = 'container-overlay'
            else: mechanism = 'decorative-overlay'
            for value in strings(row):
                for name in set(TILE_TOKEN.findall(value)) & names:
                    declared_mechanisms[name].add(mechanism); references[name].add(source)
            if kind == 'lua-tables' and isinstance(row.get('returned'), dict):
                for base, entries in row['returned'].items():
                    if base not in native: continue
                    for target in sorted(set(strings(entries)) & names):
                        table_relations.append(dict(source=base,target=target,kind=mechanism,evidence=source,targetDefined=target in native))
    jar = game / 'projectzomboid.jar'
    provenance.append(dict(path='projectzomboid.jar', sha256=sha(jar)))
    class_count = 0
    with zipfile.ZipFile(jar) as archive:
        for entry in sorted(archive.namelist()):
            if not entry.startswith('zombie/') or not entry.endswith('.class'):
                continue
            cls = class_info(archive.read(entry)); class_count += 1
            source = 'java:' + cls['name'].replace('/', '.')
            collect('\n'.join(cls['strings']), source)
            render_methods = [m for m in cls['methods'] if re.search(r'render|draw|sprite|texture|model|snow', m[0], re.I)]
            visual_calls = [c for c in cls['calls'] if re.search(r'(IsoSprite|IsoObject|Texture|SpriteRenderer|ModelDrawer|ModelRenderer)', c[0])]
            if render_methods or visual_calls:
                render_classes.append(dict(source=source, parent=cls['parent'], methods=render_methods, visualCalls=visual_calls))
                                                                                
                                                                                   
    def mechanisms(refs):
        result = set()
        for ref in refs:
            low = ref.lower()
            if 'overlays' in low: result.add('overlay-selection')
            if 'erosion' in low: result.add('erosion-growth-season')
            if 'farming' in low: result.add('farming-growth')
            if 'snow' in low: result.add('snow-selection')
            if any(s in low for s in ('isodoor', 'isowindow', 'isocurtain', 'isothumpable')): result.add('interactive-sprite-selection')
            if any(s in low for s in ('isostove', 'isofireplace', 'isobarbecue', 'isolightswitch', 'isotelevision', 'washer', 'dryer')): result.add('device-visuals')
            if any(s in low for s in ('bentfence', 'brokenfence', 'burn', 'destroy')): result.add('damage-replacement')
        return result
    relations = table_relations
    clusters, traits_count, mechanism_counts = collections.Counter(), collections.Counter(), collections.Counter()
    records = []
    geometry_names = {r['name'] for r in native_tables['geometry']}
    for name, r in sorted(native.items()):
        raw = raw_tiles.get(name, {})
        props = dict(raw.get('properties', {})); props.update(r['properties'])
        flags = set(r['flags']) | set(props)
        traits = set()
        refs = references[name]
        prefrefs = prefix_references[name.rsplit('_', 1)[0]] if '_' in name else set()
        owners = mechanisms(refs) | declared_mechanisms[name]
                                                               
        if 'Facing' in props: traits.add('orientation-label')
        if any(k in props and str(props[k]) not in ('', '0') for k in ('Noffset', 'Eoffset', 'Soffset', 'Woffset')): traits.add('orientation-relations')
        if r.get('grid'): traits.add('assembly-grid')
        if r.get('nativeModel'): traits.add('native-model')
        if name in geometry_names: traits.add('native-depth-geometry')
        if r.get('nativeModel',{}).get('animation'): traits.add('native-animation')
        if 'MoveWithWind' in props or 'MoveWithWind' in raw.get('properties',{}): traits.add('wind-transform')
        if any(k in flags for k in ('AttachedToGlass','CountertopAttach')): traits.add('attachment')
        if 'IsMoveAble' in props: traits.add('moveable')
        if 'invisible' in flags or r.get('invisible'): traits.add('invisible-footprint')
        if any(k in flags for k in ('DoubleDoor','GarageDoor')): traits.add('coordinated-assembly')
        if any(k in flags for k in ('doorN','doorW','WindowN','WindowW','windowN','windowW','IsClosedState')): traits.add('interactive-state')
        if any(k in flags for k in ('container','Container','containerType')): traits.add('container')
        if any('Light' in k or k in ('lightSwitch','signal') for k in flags): traits.add('lighting-or-device')
        if 'tree' in flags or r['type'] == 'tree': traits.add('tree')
        if 'vegitation' in flags: traits.add('vegetation')
        if 'water' in flags: traits.add('water-surface')
        if any('Roof' in k or k.startswith('roof') for k in flags): traits.add('roof-surface')
        if any(k in flags for k in ('solidfloor','FloorOverlay','diamondFloor')): traits.add('floor-surface')
        if r['type'] == 'wall' or any(k in flags for k in ('WallOverlay','WallSE','cutN','cutW')): traits.add('wall-surface')
        for prop, kind in OFFSETS.items():
            value = props.get(prop)
            if value in (None, '', '0'): continue
            try:
                sheet, index = name.rsplit('_', 1); target = f'{sheet}_{int(index)+int(value)}'
            except ValueError: continue
            relations.append(dict(source=name,target=target,kind=kind,evidence=f'property:{prop}',targetDefined=target in native))
            traits.add('orientation-relations' if kind == 'orientation' else 'state-relations')
        burnt = r.get('burntTile') or props.get('BurntTile')
        if burnt:
            traits.add('damage-target')
            relations.append(dict(source=name,target=burnt,kind='burnt-target',evidence='native:burntTile',targetDefined=burnt in native))
        def edge(target, kind, evidence):
            relations.append(dict(source=name,target=target,kind=kind,evidence=evidence,targetDefined=target in native))
        if r.get('snowSprite'):
            traits.add('snow-alternate'); owners.add('snow-selection')
            edge(r['snowSprite'],'snow-alternate','native:IsoSprite.getSnowSprite/ErosionIceQueen')
        if r['type'] in ('doorN','doorW'):
            sheet,index=name.rsplit('_',1)
            delta=8 if 'GarageDoor' in props else 4 if 'DoubleDoor' in props else 2
            edge(f'{sheet}_{int(index)+(-delta if "open" in flags else delta)}','door-counterpart','IsoDoor.<init>(IsoCell,IsoGridSquare,IsoSprite,boolean)')
            owners.add('interactive-sprite-selection')
        if 'TV' in props:
            sheet,index=name.rsplit('_',1)
            for delta in (16,32,48,64): edge(f'{sheet}_{int(index)+delta}','screen-overlay','IsoTelevision.setupDefaultScreens')
            owners.add('device-visuals')
        if name in raw_tiles:
            sheet,index=name.rsplit('_',1)
            target=f'{sheet}_on_{index}'
            if target in packed_names:
                edge(target,'powered-overlay-candidate','IsoWorld.loadedTileDefinitions; texture-existence condition')
                owners.add('powered-overlay')
                traits.add('additional-layer')
        if r.get('grid'):
            for cell in r['grid']['cells']:
                if name == r['grid']['anchor'] and name != cell[0]:
                    relations.append(dict(source=name,target=cell[0],kind='assembly-cell',evidence='native:IsoSpriteGrid',targetDefined=cell[0] in native))
        if name in extra: cluster = 'runtime-registered-sprite'
        elif r.get('invisible') or 'invisible' in flags: cluster = 'invisible-footprint'
        elif not textures[name] and not r.get('nativeModel'): cluster = 'definition-without-packed-image'
        elif 'native-model' in traits: cluster = 'native-model-binding'
        elif 'water-surface' in traits: cluster = 'water-surface'
        elif 'tree' in traits: cluster = 'tree-object'
        elif 'interactive-state' in traits or 'state-relations' in traits or 'snow-alternate' in traits: cluster = 'sprite-state-family'
        elif owners: cluster = 'referenced-visual-selection'
        elif 'assembly-grid' in traits: cluster = 'sprite-assembly'
        elif 'orientation-relations' in traits: cluster = 'oriented-sprite'
        elif any(t.endswith('-surface') for t in traits): cluster = 'world-surface'
        else: cluster = 'base-sprite-unproven-static'
        row = dict(r, definitionSources=layers[name], rawProperties=raw.get('properties', {}),
                   texturePacks=sorted(textures[name]), cluster=cluster, traits=sorted(traits),
                   mechanisms=sorted(owners), references=sorted(refs), prefixReferences=sorted(prefrefs),
                   admission='not-evaluated', staticGuarantee=False)
        records.append(row); clusters[cluster] += 1; traits_count.update(traits); mechanism_counts.update(owners)
                                                                                
                                                                  
    parent = {n:n for n in native}
    def root(n):
        while parent[n] != n: parent[n] = parent[parent[n]]; n = parent[n]
        return n
    for edge in relations:
        if edge['kind'] not in ('orientation','assembly-cell') or not edge['targetDefined']: continue
        a,b=root(edge['source']),root(edge['target']); parent[max(a,b)] = min(a,b)
    families=collections.defaultdict(list)
    for n in sorted(native): families[root(n)].append(n)
    texture_only = sorted(packed_names - set(native))
    write_jsonl(out/'tiles.jsonl.gz', records)
    write_jsonl(out/'relations.jsonl.gz', sorted(relations,key=lambda r:(r['source'],r['kind'],r['target'])))
    write_jsonl(out/'orientation-assemblies.jsonl.gz', (dict(key=k,members=v,scope='orientation-and-grid-only',validatedSlot=False) for k,v in sorted(families.items()) if len(v)>1))
    write_jsonl(out/'packed-images.jsonl.gz', texture_rows)
    write_jsonl(out/'images-without-tiledef.jsonl.gz', (dict(name=n,packs=sorted(textures[n]),references=sorted(references[n]),prefixReferences=sorted(prefix_references[n.rsplit('_',1)[0]]),mechanisms=sorted(mechanisms(references[n])),scope='packed-image-not-tiledef',admission='not-evaluated') for n in texture_only))
    write_jsonl(out/'render-api-census.jsonl.gz', render_classes)
    write_jsonl(out/'source-fingerprints.jsonl.gz', provenance+source_records)
    for kind, data in native_tables.items(): write_jsonl(out/('native-'+kind+'.jsonl.gz'),data)
    loose_images = [dict(path=p.relative_to(game).as_posix(),bytes=p.stat().st_size) for p in sorted(media.rglob('*')) if p.suffix.lower() in ('.png','.dds','.jpg','.jpeg','.tga','.pcx')]
    write_jsonl(out/'loose-images.jsonl.gz',loose_images)
    probe = json.loads(args.native.with_name('native-probe.json').read_text(encoding='utf-8'))
    summary = dict(schema=1, probe=probe, scope='installed vanilla files only; no saves, mods or runtime capture',
                   definitionRows=definition_counts, uniqueDefinedTiles=len(raw_tiles),nativeSprites=len(native),
                   runtimeSpritesOutsideDefinitions=extra, missingNativeDefinitions=missing, packEntries=packs,
                   uniquePackedImageNames=len(packed_names), packedNamesWithoutRegisteredSprite=len(texture_only),
                   packedNamesWithoutBinaryTiledef=len(packed_names-set(raw_tiles)),
                   tilesWithoutPackedImage=sum(not textures[n] for n in raw_tiles),
                   javaClassesScanned=class_count, renderRelatedClasses=len(render_classes),
                   nativeTableCounts={k:len(v) for k,v in native_tables.items()},looseImageFiles=len(loose_images),
                   textSourcesScanned=len(source_records), clusters=dict(sorted(clusters.items())),
                   traits=dict(sorted(traits_count.items())),mechanisms=dict(sorted(mechanism_counts.items())),
                   relations=len(relations),unresolvedRelationTargets=[e for e in relations if not e['targetDefined']],
                   orientationAssemblyComponents=sum(len(v)>1 for v in families.values()),
                   coverageChecks={'everyBinaryDefinitionInNativeDump':not missing,'everyNativeSpriteClassified':sum(clusters.values())==len(native),'everyPackParsed':not pack_errors},
                   limits=['Tiledef membership is exhaustive for the recorded seven loaded definition files.',
                           'Headless metadata omits GL-dependent post-load flags; powered-overlay edges explicitly remain candidates.',
                           'All installed .pack metadata is included, including alternative resolutions and unused legacy packs; pack entries are not separate semantic tiles.',
                           'Reference search scans every zombie class constant pool and vanilla Lua/script text; dynamically constructed names and runtime-only images may require explicit mechanism rules.',
                           'Classes/methods are a discovery census, not proof that every path executes in this version.',
                           'No row is automatically admitted to authoring. Unproven-static is not a guarantee of no state.',
                           'Orientation/grid components are candidates, not canonical semantic slots. States and shared destruction targets are deliberately not unioned.'])
    summary['artifacts']={p.name:dict(sha256=sha(p),bytes=p.stat().st_size) for p in sorted(out.glob('*.gz'))}
    (out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({k:v for k,v in summary.items() if k not in ('unresolvedRelationTargets','artifacts','limits','packEntries','runtimeSpritesOutsideDefinitions')},indent=2))


if __name__ == '__main__':
    main()
