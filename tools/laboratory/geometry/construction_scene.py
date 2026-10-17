"""One batch preparation of shared recipes; emitted models have no aperture runtime."""
import json
import os
import shutil
import subprocess
import numpy as np
from pathlib import Path
from .constructions import prepare
from .profile import descriptor

def build(tiles,properties,profile,interior_cells=None):
    plan=prepare(tiles,properties,interior_cells,profile)
    if not plan['requests']:return plan,[]
    node=os.environ.get('HEYZED_MAP_NODE') or shutil.which('node')
    if not node:
        candidate=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
        if candidate.exists():node=str(candidate)
    if not node:raise RuntimeError('Node.js is required to prepare shared construction models')
    d=descriptor();d['profile']=profile
    try:
        result=subprocess.run([node,str(Path(__file__).with_name('construction-compile.mjs'))],input=json.dumps(dict(requests=plan['requests'],descriptor=d)),text=True,capture_output=True,check=True)
    except subprocess.CalledProcessError as error:
        detail=next((line.strip()[7:] for line in (error.stderr or '').splitlines() if line.startswith('Error: ')),None)
        raise RuntimeError('Construction compilation failed: '+(detail or 'exit code '+str(error.returncode))) from error
    return plan,json.loads(result.stdout)

def append(assets,rows,plan,models,anchor,profile,prefix,wall_finish=None):
    origin=[anchor[0],anchor[1],anchor[2]*profile['floorHeight']]
    for i,(request,model) in enumerate(zip(plan['requests'],models)):
        identity=prefix+'construction-'+str(i);x,y,z=request['anchor'];parts=[]
        for source in model['parts']:
            part=dict(source);vertices=[]
            for j in range(0,len(source['vertices']),3):
                u,depth,height=source['vertices'][j:j+3]
                xx,yy=(u,depth) if request['side']=='N' else (depth,u)
                vertices.extend([xx+x-origin[0],yy+y-origin[1],height+z*profile['floorHeight']-origin[2]])
            vertices=np.asarray(vertices,dtype=np.float32).tolist()
            part.update(vertices=vertices,uv=source.get('uv',[0]*(len(vertices)//3*2)))
            if source.get('attachment') and source.get('normals'):
                part['normals']=[v for j in range(0,len(source['normals']),3) for v in (source['normals'][j:j+3] if request['side']=='N' else [source['normals'][j+1],source['normals'][j],source['normals'][j+2]])]
            else:part.pop('normals',None)
            if part['name']=='wall' or part['name'].startswith('wall.'):
                if wall_finish:
                    material,size=wall_finish(request.get('wallSource',request['sources'][0]));part['material']=material;part['repeat']=True
                else:size=[1,profile['floorHeight']]
                tangent=0 if request['side']=='N' else 1
                part['uv']=[v for j in range(0,len(vertices),3) for v in ((vertices[j+tangent]+origin[tangent])/size[0],(vertices[j+2]+origin[2])/size[1])]
            if request['side']=='W':
                part['indices']=list(source['indices'])
                for j in range(0,len(part['indices']),3):part['indices'][j+1],part['indices'][j+2]=part['indices'][j+2],part['indices'][j+1]
            parts.append(part)
        combined=[];indices=[]
        for part in parts:
            indices.extend(index+len(combined)//3 for index in part['indices']);combined.extend(part['vertices'])
        assets[identity]=dict(name=identity,sourceName=request['sources'][0],textureSources=request['sources'],sourceCell=[x,y,z],sourceLevel=z,
            vertices=combined,indices=indices,uv=[0]*(len(combined)//3*2),parts=parts,category=request['kind'],context=True,contextSolid=True,
            geometryProvider='laboratory.shared-constructions',construction=dict(profileId=model['profileId'],recipe=model['recipe'],state=model['state'],attachments=[dict(id=item['family']['id'],**{k:v for k,v in item.items() if k!='family'}) for item in request.get('attachments',[])]),
            profileUsage=dict(status='full',dimensions=['wallThickness','floorHeight'],basis='Approved shared construction recipe'))
        rows.append(list(anchor)+[identity,'wall',0])
