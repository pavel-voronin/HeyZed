"""Atomically publish supported tile names; no positions or world baseline."""
from pathlib import Path
import json, os, sys

def publish(source, directory):
    data = json.loads(Path(source).read_text())
    if data['schema'] != 3: raise ValueError('Resource-only furniture catalogue required')
    directory = Path(directory); directory.mkdir(parents=True, exist_ok=True)
    result = dict(schema=3, packSha256=data['packSha256'], tiles=sorted(data['bindings']))
    pending = directory/'catalog.pending'
    pending.write_text(json.dumps(result, separators=(',', ':')), encoding='utf-8')
    target = directory/'current.json'; os.replace(pending, target)
    return target

if __name__ == '__main__': print(publish(sys.argv[1], Path.home()/'.heyzed/furniture'))
