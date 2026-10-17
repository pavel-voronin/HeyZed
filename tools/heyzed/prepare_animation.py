"""Offline Unreal authoring for the native motion-matching graph."""
import json
import argparse
from pathlib import Path
import unreal

ROOT=Path(__file__).resolve().parent
manifest=ROOT/'animation-sample.json'
options=argparse.ArgumentParser()
options.add_argument('--first-person-only',action='store_true')
args=options.parse_args()
source=json.loads(manifest.read_text(encoding='utf-8'))
if args.first_person_only:
    source['groups']={key:value for key,value in source['groups'].items() if key.startswith('FP')}
    manifest=ROOT/'.local/animation/first-person.json'
    manifest.parent.mkdir(parents=True,exist_ok=True)
    manifest.write_text(json.dumps(source),encoding='utf-8')
result=unreal.HeyZedMotionAuthoring.build_library(str(manifest))
if result!='complete': raise RuntimeError(result)
output=ROOT/'client/Saved/HeyZed/animation-assets.json'
output.parent.mkdir(parents=True,exist_ok=True)
groups=json.loads(output.read_text(encoding='utf-8')).get('groups',{}) if output.exists() else {}
groups.update({name:unreal.load_asset('/Game/HeyZed/Animation/DB_'+name).get_num_animation_assets() for name in source['groups']})
output.write_text(json.dumps(dict(status='complete',groups=groups)),encoding='utf-8')
