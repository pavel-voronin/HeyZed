"""Native behavior and reviewed door appearance, independent of aperture shape."""
import json
from pathlib import Path

def descriptor():
    return json.loads(Path(__file__).with_name('door_styles.json').read_text('utf-8'))

def classify(tile):
    p=tile.get('properties', {})
    if not ('doorN' in p or 'doorW' in p) or p.get('DoorSound')=='FarmGate':
        return None
    transparent='doorTrans' in p
    sound=p.get('DoorSound', '')
    curtain=transparent and 'GarageDoor' not in p and sound.lower()!='slidingglassdoor'
    style='solid'
    if transparent:
        name=tile['name']; sheet, _, index=name.rpartition('_')
        index=int(index) if index.isdigit() else -1
        if p.get('Material2')=='MetalBars' or sound=='PrisonMetalDoor':
            style='barred'
        elif sheet=='fixtures_doors_01' and index in range(44,48) or sheet=='fixtures_doors_02' and index in range(4,8):
            style='upper-glazed'
        elif sheet=='fixtures_doors_01' and index in (*range(16,24),*range(60,64)) or sheet=='fixtures_doors_02' and index in range(56,64):
            style='small-glazed'
        elif sheet=='fixtures_doors_01' and index in (*range(32,36),*range(48,52)) or sheet=='fixtures_doors_02' and index in range(40,48) or sheet.startswith(('location_restaurant_','location_shop_fossoil_')):
            style='divided-glazed'
        elif any('Glass' in p.get(key,'') for key in ('MaterialType','Material2','Material3')):
            style='full-glazed'
        else:
            # A transparent flag alone does not establish an authored glass shape.
            return dict(typeId=None,transparent=transparent,canAddCurtain=curtain,status='Unresolved')
    return dict(typeId=style,transparent=transparent,canAddCurtain=curtain,status='Candidate')
