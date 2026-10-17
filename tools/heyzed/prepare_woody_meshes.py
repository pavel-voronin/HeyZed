"""Offline Blender conversion; no procedural plant generation runs in the client."""
from pathlib import Path
import gzip
import hashlib
import json
import bpy

ROOT = Path(__file__).resolve().parent


def extract(obj, kind, ratio):
    bpy.ops.object.select_all(action='DESELECT')
    obj.hide_set(False); obj.hide_viewport=False; obj.select_set(True); bpy.context.view_layer.objects.active=obj
    modifier=obj.modifiers.new('Game mesh simplification', 'DECIMATE'); modifier.ratio=ratio
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    mesh=obj.data; mesh.calc_loop_triangles()
    vertices=[]; groups=[[] for _ in mesh.materials]; lookup={}
    for triangle in mesh.loop_triangles:
        material=mesh.materials[triangle.material_index].name
        layer=mesh.uv_layers.get('UV_map_01') if material=='tree_small_02_branches' else mesh.uv_layers.active
        for index in triangle.loops:
            loop=mesh.loops[index]; p=mesh.vertices[loop.vertex_index].co; n=mesh.corner_normals[index].vector
            u,v=layer.data[index].uv if layer else (0,0)
            if material=='tree_small_02_branches':u*=3;v*=.6
            if material=='pine_tree_01_bark':u*=1.2;v*=.1
            # Blender Z-up to Unreal Z-up handedness; the authored root stays put.
            row=tuple(round(float(f),6) for f in (p.x*100,-p.y*100,p.z*100,n.x,-n.y,n.z,u,1-v))
            if row not in lookup:lookup[row]=len(vertices);vertices.append(row)
            groups[triangle.material_index].append(lookup[row])
    data=dict(vertices=vertices, groups=groups, materials=[m.name for m in mesh.materials],
              sourceTriangles=len(mesh.loop_triangles), units='centimetres', root='authored-local-origin')
    path=ROOT/'vegetation/woody'/(kind+'.json.gz')
    path.write_bytes(gzip.compress(json.dumps(data,separators=(',',':')).encode(),mtime=0))
    manifest_path=ROOT/'vegetation/woody/sources.json'
    manifest=json.loads(manifest_path.read_text())
    source=manifest[{'Tree':'tree_small_02','Shrub':'shrub_04','Pine':'pine_tree_01'}[kind]]
    source['geometrySha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    source['preparation']=dict(blender=bpy.app.version_string, sourceObject=obj.name, triangleRatio=ratio,
                               triangles=len(mesh.loop_triangles), root='authored-local-origin', units='centimetres')
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',newline='\n')
    print(kind,len(vertices),len(mesh.loop_triangles),'triangles',path.stat().st_size,'bytes')


bpy.ops.wm.open_mainfile(filepath=str(ROOT/'.local/vegetation-sources/tree_small_02/tree_small_02.blend'))
extract(bpy.data.objects['tree_small_02_LOD1'],'Tree',.07)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(ROOT/'.local/vegetation-sources/shrub_04/shrub_04.gltf'))
extract(next(o for o in bpy.data.objects if o.type=='MESH'),'Shrub',.45)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'.local/vegetation-sources/pine_tree_01/pine_tree_01.blend'))
extract(bpy.data.objects['pine_tree_01_a_LOD2'],'Pine',.06)
