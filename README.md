# EscherQuake

An experimental Quake 1 gravity playground built on FTEQW. Full engine source and QuakeC game source are included. Brush volumes, point fields and linked line segments change each movable entity's local up and gravity strength, with smooth transitions and gravity-relative mouse look.

This is a foundation, not a finished arbitrary-gravity conversion of Quake. The player collision hull remains axis-aligned, and stock monster AI, liquids and some scripted movement still assume world Z. Read [the engine/game boundary and limitations](docs/architecture.md) before expanding it.

The `phase-one/jolt` branch continues FTE PR 414 with a Jolt rigid-body backend and a runnable sandbox. See [phase-one build and launch instructions](docs/jolt.md). Gravity-aligned player collision remains phase two.

## Build (Linux)

On Debian/Ubuntu install `build-essential cmake zlib1g-dev libpng-dev libjpeg-dev libx11-dev libgl-dev libxcursor-dev libxrandr-dev libxxf86vm-dev libxrender-dev libxfixes-dev libxi-dev libxss-dev libxv-dev libasound2-dev libvorbis-dev libfreetype-dev libopus-dev libspeex-dev libspeexdsp-dev`.

```sh
make all JOBS=4
make test JOBS=4
make test-jolt JOBS=4
```

The binaries are `vendor/fteqw/engine/release/fteqw-gl` and `fteqw-sv`. Game code builds to `game/qwprogs.dat`. CI builds the same targets and uploads a Linux artifact. FTE's upstream build files also support other platforms; those builds have not been verified here.

## Play

Put your Quake `id1` directory (with its PAK files) in the repository root, or use a compatible [LibreQuake](https://github.com/lavenderdotpet/LibreQuake/releases) asset set. Assets are not included.

```sh
sh tools/play.sh
```

The source `.map` loads directly in FTE, so no map compiler is required for the laboratory. WASD, mouse look, space to jump, mouse 1 to fire. The optional comfort config removes bob and strafe roll. The launcher disables upstream updates so the FTE updater cannot replace this modified binary. Build and use **both** this client and server; unmodified Quake clients do not support this feature set.

The laboratory has a radial field centered at `-400 0 256`, a directional brush between X=256 and X=512, and a line field at X=640. It is intentionally roomy because the legacy collision hull does not rotate. See [mapping and API](docs/mapping.md) for keys, priority, sign conventions and QuakeC calls.

## Source layout

- `game/src/escher.qc`: field selection, priority, transitions and public helpers.
- `game/src/`: complete FTE basemod QuakeC source, with gravity-relative weapon origins, grenade arcs, jumping and landing checks.
- `vendor/fteqw/`: pinned upstream source plus engine changes for gaze transport, camera collision, friction and auto-aim.
- `tests/`: regression QuakeC run inside the dedicated engine, and native camera-math tests. Tests require no game assets.
- `docs/architecture.md`: source assessment, engine/game responsibilities and remaining work.

GPL-2.0-or-later; retain upstream notices. See [provenance](docs/provenance.md).
