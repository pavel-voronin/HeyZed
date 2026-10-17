"""Legacy triangle-stream adapter over the shared metrics and wall builder.

The native Float32 datum and face order are retained for existing consumers.
This adapter does not own architectural dimensions or invoke another runtime.
"""
import struct
from .profile import resolve, LEGACY_WALL_SKIN_CLEARANCE, LEGACY_MOUNTED_SKIN_CLEARANCE
from .openings import wall_boxes


def f32(value):
    return struct.unpack('<f',struct.pack('<f',value))[0]


def legacy_json_float(value):
    # Java's profile JSON wrote float scalars with their shortest round-trip
    # decimal; its vertex stream explicitly widened float to double.
    for precision in range(1,10):
        text=format(value,'.'+str(precision)+'g')
        if f32(float(text))==value:return float(text)
    return value


def box_vertices(boxes):
    faces=((0,3,2,1),(4,5,6,7),(0,1,5,4),(3,7,6,2),(0,4,7,3),(1,2,6,5))
    result=[]
    for x0,y0,z0,x1,y1,z1 in boxes:
        points=((x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),
                (x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1))
        for face in faces:
            for i in (0,1,2,0,2,3):result.extend(f32(v) for v in points[face[i]])
    return result


def meshes(values=None):
    p=resolve(values)
    # Java performed primitive arithmetic after rounding its inputs to float.
    q={k:f32(v) if isinstance(v,(int,float)) else v for k,v in p.items()}
    result={axis+str(opening):box_vertices(wall_boxes(axis,opening,q))
            for axis in ('north','west') for opening in (0,1,2)}
    half=f32(q['wallThickness']/2)
    skin=f32(half+f32(LEGACY_WALL_SKIN_CLEARANCE))
    result.update(floor=box_vertices([[0,0,-q['slabDepth'],q['tileSize'],q['tileSize'],0]]),
                  floorHeight=q['floorHeight'],profile=dict(
                      wallThickness=q['wallThickness'],slabDepth=q['slabDepth'],roofDepth=q['roofDepth'],
                      wallHalfExtent=skin,windowFrameHalfDepth=f32(half+q['windowFrameProjection']),
                      wallSkinOffset=skin,mountedSkinOffset=f32(skin+f32(LEGACY_MOUNTED_SKIN_CLEARANCE))))
    result['profile']={k:legacy_json_float(v) for k,v in result['profile'].items()}
    return result
