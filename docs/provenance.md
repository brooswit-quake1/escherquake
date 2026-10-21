# Provenance

- Engine and base game: [FTEQW](https://github.com/fte-team/fteqw), revision recorded in `vendor/FTE_REVISION`. The snapshot is vendored so engine changes are directly editable and the build does not depend on a moving upstream branch.
- `game/src` is derived from that revision's `quakec/basemod`, with original notices retained. The upstream copy remains under `vendor/fteqw/quakec/basemod` for comparison. Added gravity code is GPL-2.0-or-later.
- Original source assessment: [id Software Quake](https://github.com/id-Software/Quake), revision recorded in `docs/ID_QUAKE_REVISION`. No proprietary game assets are included.
- FTE includes components with their own license notices. Keep those files and notices when redistributing; the top-level GPL file does not replace component-specific notices.
- Generated map geometry and map generator are part of EscherQuake. External Quake or LibreQuake assets used for manual testing are separate and are not committed.
