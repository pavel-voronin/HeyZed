"""Prepare a single native-body crowd silhouette and small clothing atlases."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', default='C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid')
    args = parser.parse_args()
    native = ROOT / '.local/entities/native'
    destination = ROOT / '.local/entities/crowd'
    destination.mkdir(parents=True, exist_ok=True)
    catalog = json.loads((native / 'catalog.json').read_text(encoding='utf-8'))['models']
    record = catalog['media/models_x/skinned/malebody.x']
    body = json.loads((native / record['file']).read_text(encoding='utf-8'))
    body['lod_ratios'] = [.45, .20]
    body['key'] = 'crowd:painted-native-body'
    text = json.dumps(body, separators=(',', ':'))
    (destination / 'body.json').write_text(text, encoding='utf-8')
    textures = Path(args.game) / 'media/textures'
    skin = Image.open(textures / 'Body/MaleBody01.png').convert('RGBA')
    shirt = Image.open(textures / 'Clothes/Shirt_Tshirt_Textures/TShirt_White.png').convert('RGBA').resize(skin.size)
    pants = Image.open(textures / 'Clothes/Trousers_Mesh/Trousers_White.png').convert('RGBA').resize(skin.size)
    palettes = [(75,84,70), (76,82,100), (104,87,77), (97,97,91)]
    digest = hashlib.sha256(text.encode())
    for index, palette in enumerate(palettes):
        result = skin.copy()
        for layer, color in [(shirt, palette), (pants, tuple(int(v*.55) for v in palette))]:
            tinted = ImageChops.multiply(layer.convert('RGB'), Image.new('RGB', layer.size, color)).convert('RGBA')
            tinted.putalpha(layer.getchannel('A'))
            result.alpha_composite(tinted)
        result = result.resize((256,256), Image.Resampling.LANCZOS)
        result.putalpha(255)
        result.save(destination / f'outfit-{index}.png')
        digest.update(result.tobytes())
    manifest = dict(recipe='native-crowd-body-v1', digest=digest.hexdigest()[:20], vertices=len(body['positions'])//3, triangles=len(body['indices'])//3, bones=len(body['bones']), texture_size=256, materials_per_character=1, height=max(body['positions'][1::3])-min(body['positions'][1::3]))
    (destination / 'source.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest))

if __name__ == '__main__':
    main()
