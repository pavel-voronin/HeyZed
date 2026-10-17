"""Compile explicit native-XY ground profiles into shared flat mesh definitions.

No game image, alpha, atlas coordinate or old generated contour enters this module.
"""
from pathlib import Path
import hashlib
import json
import math

ROOT = Path(__file__).resolve().parent
BASE_MATERIALS=set('Road_01 Road_02 Road_03 Road_04 Road_05 Road_06 Road_07 Grass Grass_Dark Grass_Medium Grass_Light Concrete Curb Gravel Dirt Clay Dirt_Grass Burnt Sand Unknown PaintWhite PaintYellow PaintHidden'.split())


def area(poly):
    return sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(poly,poly[1:]+poly[:1]))/2


def cross(a,b,c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def triangulate(outline):
    poly=[]
    for p in outline:
        if not poly or p != poly[-1]: poly.append(p)
    if len(poly)>1 and poly[0]==poly[-1]: poly.pop()
    if area(poly)<0: poly.reverse()
    indices=list(range(len(poly))); result=[]
    while len(indices)>2:
        for i,b in enumerate(indices):
            a,c=indices[i-1],indices[(i+1)%len(indices)]
            if cross(poly[a],poly[b],poly[c])<=1e-10: continue
            if any(min(cross(poly[a],poly[b],poly[k]),cross(poly[b],poly[c],poly[k]),cross(poly[c],poly[a],poly[k]))>1e-10 for k in indices if k not in (a,b,c)): continue
            result.extend([poly[a],poly[b],poly[c]]); indices.pop(i); break
        else:
            if abs(area([poly[i] for i in indices]))>1e-8: raise ValueError('Non-simple authored ground polygon: '+str(outline))
            break
    return result


def clip(poly,axis,value,greater):
    result=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        inside=lambda p: (p[axis]-value)*(1 if greater else -1)>=-1e-10
        if inside(a): result.append(a)
        if inside(a)!=inside(b):
            t=(value-a[axis])/(b[axis]-a[axis])
            result.append([a[k]+t*(b[k]-a[k]) for k in range(2)])
    return result


def strip(path,width):
    if not path: return []
    if len(path)==1:
        x,y=path[0];h=width/2
        poly=[[x-h,y-h],[x+h,y-h],[x+h,y+h],[x-h,y+h]]
    else:
        normals=[]
        for a,b in zip(path,path[1:]):
            dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
            normals.append((-dy/length,dx/length))
        sides=[[],[]]
        for i,p in enumerate(path):
            n=normals[max(0,i-1)];m=normals[min(i,len(normals)-1)]
            denominator=1+n[0]*m[0]+n[1]*m[1]
            if denominator<.1: raise ValueError('Reversed curb path')
            delta=[(n[k]+m[k])*width/2/denominator for k in range(2)]
            for side,sign in zip(sides,[-1,1]): side.append([p[k]+sign*delta[k] for k in range(2)])
        # Square-ended paths extend past their authored ports and are clipped on cell edges.
        for i,j in [(0,1),(-1,-2)]:
            dx,dy=path[i][0]-path[j][0],path[i][1]-path[j][1];length=math.hypot(dx,dy)
            for side in sides:
                side[i]=[side[i][0]+dx/length*width,side[i][1]+dy/length*width]
        poly=sides[0]+list(reversed(sides[1]))
    for axis,value,greater in [(0,0,True),(0,1,False),(1,0,True),(1,1,False)]: poly=clip(poly,axis,value,greater) if poly else []
    # Non-corner ports share one exact section on the tile edge, independently of the local segment slope.
    # This makes a change of tangent across a cell boundary meet the same two XY endpoints.
    if path and len(path)>1:
        for port in (path[0],path[-1]):
            borders=[axis for axis in (0,1) if port[axis] in (0,1)]
            if len(borders)!=1:continue
            axis=borders[0];along=1-axis
            if not width*3<port[along]<1-width*3:continue
            ends=[i for i,p in enumerate(poly) if abs(p[axis]-port[axis])<1e-8 and abs(p[along]-port[along])<width*3]
            if len(ends)==2:
                ends.sort(key=lambda i:poly[i][along])
                poly[ends[0]][along]=port[along]-width/2
                poly[ends[1]][along]=port[along]+width/2
    return poly


def compile_catalog(properties=None):
    profiles=json.loads((ROOT/'ground-profiles.json').read_text(encoding='utf-8'))
    if properties is None:
        catalog=json.loads((ROOT.parent/'resources-lab/.local/catalog.json').read_text(encoding='utf-8'))
        properties={r['name']:r.get('properties',{}) for r in catalog['rows'] if r['kind']=='tile'}
    props=properties
    meshes={}; tiles={}
    def define(name,material,polygons,soft=0):
        parts=[]
        for poly in polygons:
            triangles=triangulate(poly)
            if not triangles: continue
            boundary=[0,0,0,0]
            if soft:
                oriented=poly if area(poly)>0 else list(reversed(poly))
                for a,b in zip(oriented,oriented[1:]+oriented[:1]):
                    if any(abs(a[k]-b[k])<1e-8 and (abs(a[k])<1e-8 or abs(a[k]-1)<1e-8) for k in (0,1)): continue
                    dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
                    if length>1e-8: boundary=[-dy/length,dx/length,(dy*a[0]-dx*a[1])/length,soft];break
            record=dict(material=material,vertices=triangles,outline=poly,boundary=boundary)
            key='GroundForm_'+hashlib.sha256(json.dumps(record,sort_keys=True).encode()).hexdigest()[:24]
            meshes[key]=record;parts.append(key)
        tiles[name]=parts
    square=[[0,0],[1,0],[1,1],[0,1]]
    half=[square,[[0,0],[1,0],[0,1]],[[0,1],[1,0],[1,1]],[[0,0],[1,1],[0,1]],[[0,0],[1,0],[1,1]],square,square,square]
    # The four registered fringe variants occupy their own edges; this is not neighbour inference.
    fringes=[[[0,0],[1,0],[1,.035],[0,.035]],[[0,0],[.035,0],[.035,1],[0,1]],[[.965,0],[1,0],[1,1],[.965,1]],[[0,.965],[1,.965],[1,1],[0,1]]]
    for name,p in props.items():
        if name.startswith(('blends_street_01_','blends_natural_01_')):
            if 'FloorMaterial' not in p:continue
            i=int(name.rsplit('_',1)[1])%16;material=p.get('FloorMaterial','Unknown');material='Grass' if material.startswith('Grass') else material
            define(name,material,[half[i] if i<8 else fringes[(i-8)%4]],.02 if name.startswith('blends_natural') and i not in (0,5,6,7) else 0)
    for family,paths in profiles['curbs'].items():
        for i,path in enumerate(paths): define(f'{family}_{i}','Curb',[strip(path,profiles['curbWidth'])] if path else [])
    materials=dict(dark_grass='Grass',light_grass='Grass',medium_grass='Grass',dirt='Dirt',sand='Sand',gravel='Road_04',street='Road_01')
    for suffix,material in materials.items():
        for i,outline in enumerate(profiles['underlays']): define(f'street_curbs_01_blend_{suffix}_{i}',material,[outline] if outline else [])
    paint=json.loads((ROOT/'ground-shapes.json').read_text(encoding='utf-8'))['overlays']
    for name,record in paint.items(): define(name,record['material'],record['patches'])
    return dict(schema=1,meshes=meshes,tiles=tiles)


if __name__=='__main__':
    output=ROOT/'.local/authored-ground.json';output.parent.mkdir(exist_ok=True)
    data=compile_catalog();output.write_text(json.dumps(data,separators=(',',':')),encoding='utf-8')
    print(f'Authored native ground catalogue: {len(data["tiles"])} tile definitions, {len(data["meshes"])} shared forms')
