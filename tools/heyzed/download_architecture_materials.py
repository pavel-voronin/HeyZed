"""Fetch a small, redistributable CC0 PBR set; keep exact source provenance."""
from pathlib import Path
import hashlib
import json
import urllib.request

ROOT=Path(__file__).resolve().parent/'architecture/materials'
ASSETS=('smooth_concrete_floor','interior_tiles','fabric_pattern_05','painted_metal_shutter','dirty_carpet','stone_block_wall')

def get(url):
    request=urllib.request.Request(url,headers={'User-Agent':'heyzed architecture preparation (CC0 asset download)'})
    with urllib.request.urlopen(request,timeout=90) as response:return response.read()

def main():
    path=ROOT/'sources.json'
    manifest=json.loads(path.read_text('utf-8'))
    known={item['id'] for item in manifest['assets']}
    for asset in ASSETS:
        if asset in known:continue
        files=json.loads(get('https://api.polyhaven.com/files/'+asset))
        record=dict(id=asset,source='https://polyhaven.com/a/'+asset,maps={})
        for kind,names in [('diff',('diff','Diffuse','col_01')),('nor_dx',('nor_dx',)),('rough',('rough','Rough'))]:
            entry=next(files[name]['1k'] for name in names if name in files)
            image=entry.get('jpg') or entry.get('png')
            url=image['url'];content=get(url)
            filename=asset+'_'+kind+Path(url).suffix
            (ROOT/filename).write_bytes(content)
            record['maps'][kind]=dict(file=filename,url=url,sha256=hashlib.sha256(content).hexdigest())
        manifest['assets'].append(record)
        pending=path.with_suffix('.pending');pending.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8');pending.replace(path)
        print(asset,flush=True)

if __name__=='__main__':main()
