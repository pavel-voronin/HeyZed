"""Author cached native clips as Unreal animation sequences; gameplay stays in PZ."""
import gc
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parent
NATIVE = ROOT / '.local/entities/native'
SOURCE = ROOT / '.local/entities/locomotion'
BASE = '/Game/HeyZed/Entities'


def main():
    catalog = json.loads((SOURCE / 'catalog.json').read_text(encoding='utf-8'))
    if catalog['errors']:
        raise RuntimeError('Native animation source extraction is incomplete')
    resources = unreal.load_asset(BASE + '/Resources')
    resources.set_editor_property('player_retargeters', {})
    models = json.loads((NATIVE / 'catalog.json').read_text(encoding='utf-8'))['models']
    candidates = []
    seen = set()
    for key, record in sorted(models.items(), key=lambda pair: len(pair[0])):
        if not record['skeletal'] or record['id'] in seen:
            continue
        seen.add(record['id'])
        native_text = (NATIVE / record['file']).read_text(encoding='utf-8')
        data = json.loads(native_text)
        bones = frozenset(bone['name'] for bone in data['bones'])
        fingerprint = hashlib.sha256(b'native-skeletal-unreduced-v3\0' + native_text.encode()).hexdigest()[:12]
        model_path = BASE + '/Meshes/SK_' + record['id'] + '_' + fingerprint
        candidates.append((model_path, bones))
    bindings = {}
    prepared_content = {}
    unavailable = []
    ignored = []
    human_id = models['media/models_x/skinned/malebody.x']['id']
    human = next(model for model in candidates if '/SK_' + human_id + '_' in model[0])
    for source_key, record in sorted(catalog['sources'].items()):
        text = (SOURCE / record['file']).read_text(encoding='utf-8')
        data = json.loads(text)
        for clip in data['clips']:
            name = clip['name'].lower()
            content = hashlib.sha256(json.dumps(clip, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            if name in prepared_content:
                if prepared_content[name] != content:
                    raise RuntimeError('Ambiguous native clip content: ' + name)
                continue
            names = frozenset(clip['tracks'])
            is_human = name.startswith(('bob_', 'zombie_'))
            key, bones = human if is_human else min(candidates, key=lambda model: (len(names - model[1]), len(model[1] - names)))
            if names - bones:
                if is_human:
                    ignored.append(dict(source=source_key, clip=clip['name'], source_only_tracks=sorted(names - bones)))
                else:
                    unavailable.append(dict(source=source_key, clip=clip['name'], missing=sorted(names - bones)))
                    continue
            model = unreal.load_asset(key)
            if not model:
                raise RuntimeError('Prepared native skeletal model is missing: ' + key)
            recipe = ('native-locomotion-v1\0' + model.get_path_name() + '\0' + clip['name'] + '\0' + text).encode()
            digest = hashlib.sha256(recipe).hexdigest()[:24]
            path = BASE + '/Animations/Locomotion/A_' + digest
            asset = unreal.HeyZedEntityResources.prepare_native_animation(text, clip['name'], model, path)
            if not asset:
                raise RuntimeError('Native animation authoring failed: ' + source_key)
            bindings[name] = asset.get_path_name()
            prepared_content[name] = content
        if len(bindings) % 25 == 0:
            gc.collect()
            unreal.SystemLibrary.collect_garbage()
            unreal.log('heyzed native locomotion clips: ' + str(len(bindings)))
    if not bindings:
        raise RuntimeError('No native locomotion resources were prepared')
    if not resources.register_native_animations(json.dumps(bindings)):
        raise RuntimeError('Native animation bindings rejected')
    if not unreal.EditorAssetLibrary.save_loaded_asset(resources, only_if_is_dirty=False):
        raise RuntimeError('Native animation resource bindings could not be saved')
    result = dict(schema=1, clips=len(bindings), unavailable=unavailable, ignored=ignored, bindings=bindings)
    (ROOT / 'client/Saved/HeyZed/native-locomotion-assets.json').write_text(json.dumps(result), encoding='utf-8')
    unreal.log('heyzed native locomotion publication complete: ' + str(len(bindings)))


main()
