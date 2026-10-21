#!/usr/bin/env python3
"""Run the shipped QuakeC in the actual dedicated server, without any Quake assets."""
from pathlib import Path
import subprocess
import sys
from make_maps import generate

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
generate(BUILD / "testgame/maps")


def run(command, cwd=ROOT):
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    if result.returncode:
        print(result.stdout)
        raise SystemExit(result.returncode)
    return result.stdout


run([str(ROOT / "vendor/fteqw/engine/qclib/fteqcc.bin"), "-I../game/src", "-srcfile", "progs.src"], ROOT / "tests")
run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "tests/view_test.c", "-lm", "-o", "build/view_test"])
print(run([str(BUILD / "view_test")]).strip())
try:
    output = run([str(ROOT / "vendor/fteqw/engine/release/fteqw-sv"), "-nohome", "-quake",
                  "-basedir", str(BUILD), "-game", "testgame", "+set", "sv_public", "0",
                  "+set", "developer", "1", "+map", "maps/escher_lab.map"])
except subprocess.TimeoutExpired as exc:
    print(exc.stdout)
    raise SystemExit("Server tests timed out")
(BUILD / "gravity-tests.log").write_text(output)
if "ESCHER TESTS PASSED:" not in output or any(s in output for s in ("FAIL:", "Failed to load", "Unknown builtin", "gravity regression")):
    print(output)
    sys.exit(1)
print(next(line for line in output.splitlines() if "ESCHER TESTS PASSED:" in line))
