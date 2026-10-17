"""Export the existing laboratory source inventory for native Unreal authoring."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def category(name, p):
    if name.startswith('appliances_com_01_') and 136 <= int(name.rsplit('_', 1)[1]) <= 141: return 'Utility poles'
    label = (p.get('GroupName', '') + ' ' + p.get('CustomName', '')).lower()
    if 'sign' in label or name.startswith(('advertising_', 'location_shop')): return 'Signs'
    if 'IsTableTop' in p and ('lamp' in label or name.startswith('lighting_')): return 'Table lamps'
    if 'streetlight' in p and name.startswith('lighting_outdoor'): return 'Outdoor lights'
    if 'lamp' in label and 'outdoor' in label: return 'Outdoor lights'
    if 'WallObject' == p.get('MoveType') or 'wall' in label: return 'Wall lights'
    if 'switch' in label or 'chandelier' in label: return 'Room lights'
    if name.startswith('lighting_'): return 'Indoor lights'
    return 'Other stationary sources'

def prepare():
    source=json.loads((ROOT.parent/'resources-lab/.local/catalog.json').read_text(encoding='utf-8'))
    entries=[]
    profiles={'Table lamps':'table','Room lights':'room','Signs':'sign','Outdoor lights':'street','Utility poles':'street'}
    for row in source['rows']:
        p=row.get('properties',{});name=row['name']
        if row['kind']!='tile' or ('lightswitch' not in p and float(p.get('LightRadius',0))<=0):continue
        if name.startswith(('vehicles_','car_','items_')):continue
        group=category(name,p)
        profile='switch' if name.startswith('lighting_') and 'lightR' not in p and 'lightswitch' in p else profiles.get(group)
        if profile=='street' and (any('attached'+axis in p for axis in 'NWSE') or not name.startswith('lighting_outdoor_')):
            profile='wall'
        entries.append(dict(tile=name,category=group,properties=p,profile=profile,status='representative' if profile else 'unassigned'))
    out=ROOT/'.local/lighting/fixture-catalog.json';out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(dict(schema=1,source='light-lab',entries=entries),separators=(',',':')),encoding='utf-8')
    print(f'Prepared {len(entries)} laboratory fixture records',flush=True)
    return entries
if __name__=='__main__':prepare()
