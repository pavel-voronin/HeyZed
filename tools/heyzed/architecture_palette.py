"""Small shared finish palette; inferred assignments never alter the review book."""
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parent
FAMILIES={
 'Brick':('brick_wall_006',('Clay','Cream','Dark')),
 'Plaster':('plastered_wall_02',('Light','Cream','Blue','Green','Rose','Dark')),
 'Wood':('wood_planks_dirt',('Cream','Warm','Dark')),
 'Metal':('painted_metal_shutter',('Light','Blue','Dark')),
 'Tile':('interior_tiles',('Light','Cream','Blue','Green','Dark')),
 'Concrete':('smooth_concrete_floor',('Light','Warm','Dark')),
 'Stone':('stone_block_wall',('Light','Cream','Warm','Dark')),
 'Carpet':('dirty_carpet',('Warm','Blue','Green','Rose')),
 'Fabric':('fabric_pattern_05',('Light','Cream','Blue','Green','Rose','Dark')),
}
COLORS=dict(Light='#cececa',Cream='#c9bd9e',Warm='#9a7954',Clay='#ab7052',
            Blue='#849eaf',Green='#849876',Rose='#b38e91',Dark='#575957')

def rgb(value):
    return tuple(int(value[i:i+2],16)/255 for i in (1,3,5))

def family(name):
    name=name.lower()
    for key,words in [('Carpet',('carpet','rug')),('Fabric',('fabric','cloth','curtain')),
                      ('Wood',('wood','timber','plank','logwall','paneling')),
                      ('Metal',('metal','corrugat','steel','iron')),
                      ('Tile',('tile','ceramic','marble','terrazzo')),
                      ('Brick',('brick','masonry')),('Stone',('stone','rock')),('Concrete',('concrete','cement','block'))]:
        if any(word in name for word in words):return key
    return 'Plaster'

def build(definitions):
    from PIL import Image
    palette={group+'_'+tone:dict(asset=asset,color=COLORS[tone],width=.5 if group in ('Fabric','Tile') else 2,
                               metallic=.75 if group=='Metal' else 0)
             for group,(asset,tones) in FAMILIES.items() for tone in tones}
    bindings={};evidence={}
    def assign(name,record):
        group=record.get('family') or family(name+' '+record.get('appearanceFamily',''))
        color=rgb(record.get('color','#b6b0a4'))
        tone=min(FAMILIES[group][1],key=lambda t:sum((a-b)**2 for a,b in zip(color,rgb(COLORS[t]))))
        bindings[name]=group+'_'+tone
        evidence[name]=dict(sourceMaterial=name,palette=bindings[name],basis='Provisional semantic family and nearest shared colour')
    for name,record in definitions['materials'].items():assign(name,record)
    book=json.loads((ROOT.parent/'materials-lab/materials.json').read_text('utf-8'))
    for tile,material in book['tiles'].items():
        if material in bindings:bindings[tile]=bindings[material]
    for tile,source in definitions['sources'].items():
        if tile not in bindings:assign(tile,dict(initialReference=tile,color='#b6b0a4'))
    catalogue=json.loads((ROOT.parent/'resources-lab/.local/catalog.json').read_text('utf-8'))
    for tile in catalogue['rows']:
        properties=tile.get('properties',{})
        if tile['kind']!='tile' or 'solidfloor' not in properties or tile['name'] in bindings:continue
        surface=properties.get('FootstepMaterial',properties.get('CustomName',''))
        assign(tile['name'],dict(family=family(surface),color='#b6b0a4'))
    palette.update(Floor=palette['Wood_Warm'],Brick=palette['Brick_Clay'],Plaster=palette['Plaster_Light'],
                   Wood=palette['Wood_Warm'],Metal=palette['Metal_Dark'],Fabric=palette['Fabric_Cream'],
                   Frame=palette['Plaster_Cream'],Cut=palette['Plaster_Cream'])
    manifest=json.loads((ROOT/'architecture/materials/sources.json').read_text('utf-8'))
    averages={}
    def linear(v):return v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
    for asset in manifest['assets']:
        with Image.open(ROOT/'architecture/materials'/asset['maps']['diff']['file']) as image:
            pixels=list(image.convert('RGB').resize((64,64)).getdata())
        averages[asset['id']]=[sum(linear(p[i]/255) for p in pixels)/len(pixels) for i in range(3)]
    for spec in palette.values():
        spec['tint']=[min(8,max(.04,linear(target)/max(.01,mean))) for target,mean in zip(rgb(spec['color']),averages[spec['asset']])]
    return dict(schema=1,palette=palette,bindings=bindings,evidence=evidence)

if __name__=='__main__':
    data=json.loads((ROOT/'.local/architecture-world/definitions.json').read_text('utf-8'))
    target=ROOT/'.local/architecture-world/palette.json'
    target.write_bytes(json.dumps(build(data),separators=(',',':')).encode('utf-8'))
