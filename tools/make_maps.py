#!/usr/bin/env python3
"""Generate asset-free .map fixtures. FTE can load these directly via TERRAIN support."""
from pathlib import Path
import struct


def box(lo, hi, texture="escher_grid"):
    x, y, z = lo
    X, Y, Z = hi
    faces = [((x,y,z),(x,y,Z),(x,Y,z)), ((X,y,z),(X,Y,z),(X,y,Z)),
             ((x,y,z),(X,y,z),(x,y,Z)), ((x,Y,z),(x,Y,Z),(X,Y,z)),
             ((x,y,z),(x,Y,z),(X,y,z)), ((x,y,Z),(X,y,Z),(x,Y,Z))]
    return "{\n" + "\n".join(" ".join(f"( {a} {b} {c} )" for a,b,c in reversed(face))
        + f" {texture} 0 0 0 1 1" for face in faces) + "\n}\n"


def entity(values, brushes=""):
    return "{\n" + "".join(f'"{key}" "{value}"\n' for key,value in values.items()) + brushes + "}\n"


def generate(destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    walls = [((-1024,-512,-64),(1024,512,0)), ((-1024,-512,512),(1024,512,576)),
             ((-1088,-576,-64),(-1024,576,576)), ((1024,-576,-64),(1088,576,576)),
             ((-1024,-576,-64),(1024,-512,576)), ((-1024,512,-64),(1024,576,576))]
    geometry = "".join(box(a,b) for a,b in walls)
    # Solid cores let the player walk around the radial and line fields.
    geometry += box((-464,-64,192),(-336,64,320), "escher_point")
    geometry += box((608,-256,224),(672,256,288), "escher_line")
    content = entity({"classname":"worldspawn", "message":"EscherQuake gravity laboratory"}, geometry)
    content += entity({"classname":"info_player_start", "origin":"-800 0 32", "angle":"0"})
    content += entity({"classname":"trigger_gravity", "gravity_up":"-1 0 0", "gravity_strength":"600",
                       "gravity_transition":"1", "priority":"10"}, box((256,-256,0),(512,256,512),"trigger"))
    content += entity({"classname":"info_gravity", "origin":"-400 0 256", "gravity_radius":"300",
                       "gravity_strength":"500", "gravity_transition":"1"})
    content += entity({"classname":"info_gravity", "origin":"640 -256 256", "target":"rail_end",
                       "gravity_radius":"200", "gravity_strength":"600", "gravity_transition":"1"})
    content += entity({"classname":"info_gravity_target", "targetname":"rail_end", "origin":"640 256 256"})
    content += entity({"classname":"info_gravity", "origin":"0 -300 256", "gravity_radius":"180",
                       "gravity_strength":"300", "gravity_inward":"1", "priority":"5"})
    content += entity({"classname":"trigger_gravity", "gravity_up":"0 0 -1", "priority":"20",
                       "gravity_transition":"1.5"}, box((-128,256,0),(128,512,512),"trigger"))
    wedge = box((720,320,0),(960,480,256), "trigger")
    wedge = wedge.rsplit("}",1)[0] + "( 960 320 0 ) ( 800 480 256 ) ( 800 480 0 ) trigger 0 0 0 1 1\n}\n"
    content += entity({"classname":"trigger_gravity", "targetname":"wedge_test",
                       "gravity_disabled":"1"}, wedge)
    (destination / "escher_lab.map").write_text(content)
    sandbox = content
    for i in range(6):
        sandbox += entity({"classname":"misc_jolt_box", "origin":f"{-800+(i%3)*40} {-180+(i//3)*40} {100+(i//3)*50}", "angles":"10 20 15"})
    for i in range(4):
        sandbox += entity({"classname":"misc_jolt_capsule", "origin":f"{-660+i*50} 120 150", "angles":f"{i*30} 20 0"})
    sandbox += entity({"classname":"misc_jolt_capsule", "origin":"-400 150 300", "angles":"60 0 0"})
    (destination / "jolt_lab.map").write_text(sandbox)



def textures(destination):
    """Small original grid textures; no external WAD or game art needed for the lab geometry."""
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for name, color in [("escher_grid", (70,80,90)), ("escher_point", (210,130,40)),
                        ("escher_line", (40,150,190))]:
        pixels = bytearray()
        for y in range(64):
            for x in range(64):
                c = (210,220,225) if x < 2 or y < 2 else color
                pixels.extend(reversed(c))  # uncompressed 24-bit BGR TGA
        header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 64, 64, 24, 0x20)
        (destination / f"{name}.tga").write_bytes(header + pixels)


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv)>1 else "game/maps")
    if len(sys.argv) <= 1 or sys.argv[1] == "game/maps":
        textures("game/textures")
