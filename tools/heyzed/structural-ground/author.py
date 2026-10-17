"""Bounded native Geometry Script experiment; does not touch the working map."""
import hashlib
import json
import math
from pathlib import Path
import sys
import unreal

ROOT = Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
from prepare import boundary_points

OUTPUT = ROOT.parent/'.local/structural-ground'
recipe = json.loads((ROOT/'calibration.json').read_text('utf-8'))
identity = hashlib.sha256((ROOT/'calibration.json').read_bytes()+(ROOT/'author.py').read_bytes()+(ROOT/'prepare.py').read_bytes()).hexdigest()[:16]
DEST = '/Game/StructuralGround/'+identity
primitive = unreal.GeometryScript_Primitives
query = unreal.GeometryScript_MeshQueries
spatial = unreal.GeometryScript_MeshSpatial
assets = unreal.GeometryScript_NewAssetUtils
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
keep_alive = []


def material(name,color):
    path = DEST+'/M_'+name
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset:
        return asset
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_'+name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    node = unreal.MaterialEditingLibrary.create_material_expression(asset,unreal.MaterialExpressionConstant3Vector)
    node.set_editor_property('constant',unreal.LinearColor(*color,1))
    unreal.MaterialEditingLibrary.connect_material_property(node,'',unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


materials = {name:material(name,color) for name,color in dict(Grass=(.11,.22,.07),Road_01=(.065,.07,.075),Curb=(.45,.43,.37)).items()}
results = []


def save_mesh(mesh,name,material_name,position):
    borders = query.get_num_open_border_edges(mesh)
    area,volume = query.get_mesh_volume_area(mesh)
    if borders != 0 or volume <= 0:
        raise RuntimeError(f'{name}: invalid solid, border edges={borders}, volume={volume}')
    path = DEST+'/SM_'+name
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not asset:
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions(enable_nanite=True,enable_collision=False)
        options.set_editor_property('nanite_settings',unreal.MeshNaniteSettings(enabled=True))
        asset,outcome = assets.create_new_static_mesh_asset_from_mesh(mesh,path,options)
        if not asset or outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError('Native static mesh creation failed: '+name)
    asset.set_material(0,materials[material_name])
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Native save failed: '+path)
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(position,0,0))
    actor.static_mesh_component.set_static_mesh(asset)
    actor.set_actor_label(name)
    keep_alive.extend([mesh,asset,actor])
    nanite = asset.get_editor_property('nanite_settings')
    if not nanite.enabled:
        raise RuntimeError('Nanite flag missing: '+name)
    return dict(asset=path,triangles=mesh.get_triangle_count(),openBorderEdges=borders,surfaceAreaCm2=area,volumeCm3=volume,naniteEnabled=nanite.enabled)


for level_index,detail in enumerate(recipe['detail']):
    points,tangents = boundary_points(recipe,detail)
    width = recipe['boundary']['width']
    left = [[p[0]-t[1]*width/2,p[1]+t[0]*width/2] for p,t in zip(points,tangents)]
    right = [[p[0]+t[1]*width/2,p[1]-t[0]*width/2] for p,t in zip(points,tangents)]
    xmin,ymin,xmax,ymax = recipe['domain']
    regions = dict(lawn=left+[[xmin,ymax]],pavement=[[xmin,ymin],[xmax,ymin],[xmax,ymax]]+list(reversed(right)))
    record = dict(level=detail['id'],maxArcChordErrorMeters=recipe['boundary']['arc']['radius']*(1-math.cos(math.radians(detail['arcStepDegrees']/2))),regions=[],boundaryOwner=recipe['boundary']['id'])
    for name,outline in regions.items():
        surface = recipe['surfaces'][name]
        mesh = unreal.DynamicMesh()
        poly = [unreal.Vector2D(x*100,y*100) for x,y in outline]
        primitive.append_simple_extrude_polygon(mesh,unreal.GeometryScriptPrimitiveOptions(),unreal.Transform(location=unreal.Vector(0,0,-15)),poly,(surface['height']+.15)*100)
        record['regions'].append(save_mesh(mesh,detail['id']+'_'+name,surface['material'],level_index*3000))
    mesh = unreal.DynamicMesh()
    profile = recipe['boundary']['profile'] if detail['bevel'] else [[-width/2,-.15],[width/2,-.15],[width/2,.15],[-width/2,.15]]
    frames = [unreal.Transform(location=unreal.Vector(p[0]*100,p[1]*100,0),rotation=unreal.Rotator(pitch=0,yaw=math.degrees(math.atan2(t[1],t[0])),roll=0)) for p,t in zip(points,tangents)]
    primitive.append_sweep_polygon(mesh,unreal.GeometryScriptPrimitiveOptions(),unreal.Transform(),[unreal.Vector2D(s*100,z*100) for s,z in profile],frames)
    record['curb'] = save_mesh(mesh,detail['id']+'_curb',recipe['boundary']['material'],level_index*3000)
    mesh,bvh = spatial.build_bvh_for_mesh(mesh)
    rays = []
    x = -800
    y = recipe['boundary']['start'][1]*100
    for label,origin,direction in [('road-side',unreal.Vector(x,y-100,8),unreal.Vector(0,1,0)),('lawn-side-reverse',unreal.Vector(x,y+100,8),unreal.Vector(0,-1,0))]:
        mesh,hit,outcome = spatial.find_nearest_ray_intersection_with_mesh(mesh,bvh,origin,direction,unreal.GeometryScriptSpatialQueryOptions())
        if not hit.hit:
            raise RuntimeError('Missing reverse side: '+detail['id']+' '+label)
        rays.append(dict(side=label,hit=hit.hit,distanceCm=hit.ray_parameter))
    record['opposedRays'] = rays
    record['surfaceHeightsMeters'] = {k:v['height'] for k,v in recipe['surfaces'].items()}
    results.append(record)

level_path = DEST+'/Comparison'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not unreal.EditorLoadingAndSavingUtils.save_map(world,level_path):
    raise RuntimeError('Native comparison level save failed')
report = dict(engine=unreal.SystemLibrary.get_engine_version(),recipeSha256=identity,description='Authored calibration only. No native map recognition, viewport capture or gameplay performance acceptance.',levels=results,level=level_path,sourceImageBakes=0,workingWorldChanged=False)
(OUTPUT/'native-result.json').write_text(json.dumps(report,indent=2),'utf-8')
unreal.log('STRUCTURAL_GROUND_SUCCESS '+json.dumps(report,separators=(',',':')))
