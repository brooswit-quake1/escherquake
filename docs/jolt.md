# Jolt integration: phase one

This branch continues [erysdren's FTE Jolt draft, PR 414](https://github.com/fte-team/fteqw/pull/414). The imported patch is a separate commit retaining its author. Jolt is pinned to v5.5.0; its MIT license and revision are in `vendor/jolt/`. This is a development branch for a future upstream FTE contribution.

## Build and run on codey

Linux is the tested platform. Install the dependencies from the root README, plus CMake 3.20 or newer. Then:

```sh
git fetch origin
git switch phase-one/jolt
make all JOBS=4
make test JOBS=4
make test-jolt JOBS=4
sh tools/play-jolt.sh
```

Put a Quake or compatible LibreQuake `id1` directory in the repository root. The automated tests need no external assets. The graphical sandbox uses original generated box and capsule models plus your installed Quake assets for the player and ordinary game resources.

`play-jolt.sh` loads the Jolt plugin explicitly and opens `jolt_lab.map`. Boxes and capsules exercise rigid-body collision and the existing gravity fields. The player still uses FTE's existing movement collision. Gravity-aligned **player** capsules and shared client prediction are phase two.

To load Jolt in a different map, start this build of FTE and use `plug_load jolt` before loading the map. The module is `vendor/fteqw/engine/release/fteplug_jolt.so`. Use the matching engine: the rigid-body plugin ABI changes in this branch require recompiling plugins.

## Engine and game boundary

The engine owns shape construction and orientation, simulation timing, BSP collision conversion, traces, physics state lifetime, and bounds used for triggers and visibility. QuakeC supplies ordinary entity properties: `movetype`, `solid`, `geomtype`, bounds, mass, angles, velocity, gravity direction and multiplier. The sandbox entity classes in `game/src/jolt.qc` are examples, not dependencies of the backend.

Gravity field selection, priority, radial/segment geometry, and transitions remain in `game/src/escher.qc`. `MOVETYPE_PHYSICS` entities now participate in those fields. A freely rotating physical capsule is distinct from the controlled, gravity-aligned player capsule planned for phase two.

## Quake 1 collision

Q1 BSPs lack the original brush list. This branch enumerates convex solid cells from the unexpanded hull-zero BSP, bounded by each model's extents, and presents them through FTE's brush enumeration interface. The Jolt adapter creates compound collision shapes. Inline models use their own head node, preserving concave doors and platforms. Uncompiled `.map` brushes and BIH brushes expose their existing brush planes through the same interface. Heightfield sections and curved patches need separate mesh adapters.

Clip-only geometry found only in the historical expanded player hulls is not represented by hull zero. Recovering or explicitly authoring that geometry is a remaining compatibility issue for the future capsule player controller. Liquids remain contents volumes handled by FTE/game logic. Their surfaces do not become solid Jolt walls.

The BSP adapter has a 256-plane path limit; malformed or unusually deep maps require additional handling. This is not a claim of compatibility with every historical BSP extension or mod.

## Remaining work beyond the rigid-body core

The draft's wishlist also includes ragdolls, buoyancy, physical particles, debug drawing, custom QC hulls/compounds, and additional joint types. Those features must not be confused with tested core integration. Moving platform behavior and network prediction for a Jolt character controller need dedicated work in phase two. ODE and Bullet keep their existing native trace paths rather than opting into Jolt's new trace callback.

See the test source and CI for the exact validation performed. Graphical launch in a virtual display establishes startup, not subjective controller comfort or GPU performance on codey.

## Validation

- Dedicated engine tests exercise 26 assertions per map run, plus save/load reconstruction, then repeat after a map reload. The matrix covers both movement modes and three configured server tick intervals, plus an uncompiled `.map` fixture.
- The fixtures check oriented and asymmetric capsules, explicit X/Y axes, cylinder height, concave rotated BSP submodels, point and box traces, owner/dimension filtering, fast-body floor collision, rotated bounds, mass changes with attached constraints, and main-thread QuakeC contact callbacks.
- The existing 131 gravity checks and camera transport checks pass.
- Linux client and server compile; the graphical Jolt sandbox launches under Xvfb with software OpenGL and LibreQuake assets.
- The ODE adapter passes a syntax check after repairing three reversed dynamic symbol entries and a missing anchor function declaration/entry found during compatibility review. ODE/Bullet runtime parity is not claimed.
