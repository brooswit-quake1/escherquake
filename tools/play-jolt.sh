#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
client="$root/vendor/fteqw/engine/release/fteqw-gl"
if [ ! -x "$client" ] || [ ! -f "$root/vendor/fteqw/engine/release/fteplug_jolt.so" ] || [ ! -f "$root/game/qwprogs.dat" ]; then
    echo 'Build the engine, Jolt and game first: make all' >&2
    exit 1
fi
exec "$client" -nohome -noupdates -quake -basedir "$root" -game game +plug_load jolt +exec escher.cfg +map maps/jolt_lab.map "$@"
