#!/usr/bin/env python3
"""Small, asset-free BSP29 room exercising the actual Q1 BSP loader and hulls."""
from pathlib import Path
import struct


def generate(path):
    pack = struct.pack
    lumps = [b'' for _ in range(15)]
    lumps[0] = b'{\n"classname" "worldspawn"\n}\n\0'
    # Front is the room interior; the back of each plane is solid.
    planes = [(1,0,0,-256,0), (-1,0,0,-256,3), (0,1,0,-256,1),
              (0,-1,0,-256,4), (0,0,1,0,2), (0,0,-1,-256,5)]
    lumps[1] = b''.join(pack('<4fi', *p) for p in planes)
    lumps[2] = pack('<ii', 1, -1)  # use the engine's missing-texture material
    lumps[3] = b''.join(pack('<3f',*v) for v in [(-256,-256,0),(256,-256,0),(256,256,0),(-256,256,0)])
    lumps[6] = pack('<8f2i',1,0,0,0,0,1,0,0,0,0)
    lumps[7] = pack('<HHiHH4Bi',4,0,0,4,0,255,255,255,255,-1)
    lumps[11] = pack('<H',0)
    lumps[13] = pack('<4i',1,2,3,4)
    for i in range(6):
        front = i+1 if i<5 else -2  # negative node children encode leaf index
        lumps[5] += pack('<i2h6h2H', i, front, -1, -272,-272,-16,272,272,272,0,1)
        front = i+1 if i<5 else -1  # clipnodes instead use negative contents
        lumps[9] += pack('<i2h', i, front, -2)
    for contents in (-2,-1):
        lumps[10] += pack('<ii6h2H4B', contents, -1, -272,-272,-16,272,272,272,0,1 if contents==-1 else 0,0,0,0,0)
    lumps[12] = b''.join(pack('<2H',*e) for e in [(0,0),(0,1),(1,2),(2,3),(3,0)])
    lumps[14] = pack('<9f7i', -272,-272,-16,272,272,272,0,0,0,0,0,0,0,1,0,1)
    # Inline model *1: an L, whose empty corner must remain empty in Jolt.
    extra = [(1,0,0,-64,0),(-1,0,0,-64,3),(0,1,0,-64,1),
             (0,-1,0,-64,4),(0,0,1,-8,2),(0,0,-1,-8,5),
             (1,0,0,-32,0),(0,1,0,-32,1)]
    lumps[1] += b''.join(pack('<4fi', *p) for p in extra)
    for j in range(8):
        index = 6+j
        front,back = (index+1,-2) if j<6 else ((13,-1) if j==6 else (-2,-1))
        lumps[5] += pack('<i2h6h2H',index,front,back,-64,-64,-8,64,64,8,0,0)
        # leaf indices map to CONTENTS_EMPTY=-1 and CONTENTS_SOLID=-2
        cf,cb = (front if front>=0 else (-1 if front==-2 else -2)), (back if back>=0 else (-1 if back==-2 else -2))
        lumps[9] += pack('<i2h',index,cf,cb)
    lumps[14] += pack('<9f7i',-64,-64,-8,64,64,8,0,0,0,6,6,6,6,1,0,0)
    offset = 4 + 15*8
    header = pack('<i',29)
    data = b''
    for lump in lumps:
        padding = (-offset) % 4
        data += b'\0' * padding
        offset += padding
        header += pack('<ii',offset,len(lump))
        data += lump
        offset += len(lump)
    path = Path(path)
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(header+data)

def generate_brush_map(path):
    from make_maps import box, entity
    walls = [((-272,-272,-16),(272,272,0)),((-272,-272,256),(272,272,272)),
             ((-272,-272,0),(-256,272,256)),((256,-272,0),(272,272,256)),
             ((-256,-272,0),(256,-256,256)),((-256,256,0),(256,272,256))]
    text = entity({"classname":"worldspawn"}, ''.join(box(a,b) for a,b in walls))
    text += entity({"classname":"jolt_inline_fixture"}, box((-64,-64,-8),(64,-32,8)) + box((-64,-32,-8),(-32,64,8)))
    Path(path).write_text(text)

if __name__ == '__main__':
    import sys
    generate(sys.argv[1])
