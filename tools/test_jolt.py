#!/usr/bin/env python3
"""Exercise the real FTE/Jolt module and QuakeC through Q1 BSP load/save/reload."""
from pathlib import Path
import subprocess
from make_physics_bsp import generate, generate_brush_map

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/'build'
GAME = BUILD/'jolttest'
GAME.mkdir(parents=True,exist_ok=True)
generate(GAME/'maps/jolt_room.bsp')
generate_brush_map(GAME/'maps/jolt_room.map')
(GAME/'quake.rc').write_text('// Isolated test startup.\n')
qcc = ROOT/'vendor/fteqw/engine/qclib/fteqcc.bin'
compiled = subprocess.run([str(qcc),'-I../game/src','-srcfile','jolt_progs.src'],cwd=ROOT/'tests',stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(BUILD/'jolt-qc-build.log').write_text(compiled.stdout)
if compiled.returncode:
    raise SystemExit(compiled.stdout)
server = ROOT/'vendor/fteqw/engine/release/fteqw-sv'
cases = [(nq,tick,'jolt_room') for nq in (0,1) for tick in (0.005,0.013,0.05)]
cases.append((0,0.013,'maps/jolt_room.map'))
for nq,tick,mapname in cases:
    args = [str(server),'-nohome','-noupdates','-quake','-basedir',str(BUILD),'-game','jolttest',
            '+set','developer','1','+set','sv_public','0','+set','sv_nqplayerphysics',str(nq),
            '+set','sv_mintic',str(tick),'+set','sv_maxtic',str(tick),
            '+set','jolt_reload_marker','0','+plug_load','jolt','+map',mapname]
    try:
        result = subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f'Jolt test timed out: {exc.stdout}')
    name=f'jolt-nq{nq}-tick{tick}-'+('map' if mapname.endswith('.map') else 'bsp')+'.log'
    (BUILD/name).write_text(result.stdout)
    if result.returncode or result.stdout.count('JOLT TESTS PASSED:') != 2 or 'JOLT SAVELOAD PASSED' not in result.stdout or any(x in result.stdout for x in ('JOLT FAIL:', 'signal SIG','Jolt:', 'Unable to load','not compatible', 'NULL function')):
        raise SystemExit(result.stdout)
    print(f'Jolt: {mapname}, nq={nq}, tick={tick}: collision, timing, save/load and map reload passed',flush=True)
