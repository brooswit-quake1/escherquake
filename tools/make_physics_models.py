#!/usr/bin/env python3
"""Generate original, single-frame Quake MDL primitives for the Jolt sandbox."""
from pathlib import Path
import math
import struct


def write_model(path, vertices, triangles, color):
    pack = struct.pack
    lo = [min(v[i] for v in vertices) for i in range(3)]
    hi = [max(v[i] for v in vertices) for i in range(3)]
    scale = [(hi[i]-lo[i])/255 for i in range(3)]
    data = pack('<ii3f3ff3f8if', 0x4F504449, 6, *scale, *lo, 64, 0,0,0,
                1,16,16,len(vertices),len(triangles),1,0,0,1)
    data += pack('<i',0) + bytes(color + (8 if (x//4+y//4)%2 else 0) for y in range(16) for x in range(16))
    data += b''.join(pack('<3i',0,(i%16),((i//16)%16)) for i in range(len(vertices)))
    data += b''.join(pack('<4i',1,*tri) for tri in triangles)
    data += pack('<i',0) + bytes([0,0,0,0,255,255,255,0]) + b'primitive'.ljust(16,b'\0')
    for v in vertices:
        data += bytes([max(0,min(255,round((v[i]-lo[i])/scale[i]))) for i in range(3)] + [0])
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data)


def generate(destination):
    destination = Path(destination)
    verts = [(x,y,z) for z in (-16,16) for y in (-16,16) for x in (-16,16)]
    faces = [(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)]
    tris = [(f[0],f[i],f[i+1]) for f in faces for i in (1,2)]
    write_model(destination/'jolt_box.mdl',verts,tris,176)
    verts=[]
    rings=[(-8,-math.pi/2+i*math.pi/10) for i in range(6)] + [(16,i*math.pi/10) for i in range(6)]
    for center,lat in rings:
        for j in range(12):
            phi=j*math.tau/12
            verts.append((16*math.cos(lat)*math.cos(phi),16*math.cos(lat)*math.sin(phi),center+16*math.sin(lat)))
    tris=[]
    for ring in range(len(rings)-1):
        for j in range(12):
            a=ring*12+j; b=ring*12+(j+1)%12; c=a+12; d=b+12
            tris += [(a,b,c),(b,d,c)]
    write_model(destination/'jolt_capsule.mdl',verts,tris,112)

if __name__=='__main__':
    import sys
    generate(sys.argv[1])
