# Engine and game responsibilities

## Assessment of the original engine

The original id Software source was inspected alongside FTE (revision recorded in `ID_QUAKE_REVISION`). Simply replacing `velocity_z -= gravity` is insufficient:

| Concern | Original assumption | Correct owner |
| --- | --- | --- |
| Acceleration and grounding | `WinQuake/sv_phys.c`: gravity, floor normals, stairs and toss movement use Z | Engine |
| Player input | `WinQuake/sv_user.c`, `QW/client/pmove.c`: yaw plane, friction, jump, step and water movement | Shared engine movement code, used by server and predictor |
| Entity state | Scalar gravity cannot represent a local frame | Engine's per-entity gravity direction and network state |
| Collision | Q1 BSP precomputed standing/large hulls are axis-aligned | Engine and map collision format |
| View | World yaw/pitch, Z view offset, roll and pitch limits | Client engine |
| Weapons and damage | QuakeC muzzle offsets, grenade arcs, fall-speed sampling | Game |
| Fields | Brush shapes, point/segment rules, priority, toggles, affected entity policy | Game |
| Transition settings | Desired turn duration, magnitude easing, entry/exit policy | Game; applied through the engine's gravity primitive |
| Monsters | Locomotion is partly engine, behavior and attack assumptions are QuakeC | Both, with distinct changes |

## Why FTE

FTE already has a normalized `.gravitydir`, scalar `.gravity`, gravity-relative shared player movement, steps/jumps, gravity-aware server toss/bounce movement, gravity direction in replacement deltas, and gravity-relative client input and eye placement. Reusing these avoids creating a second player controller or applying forces in QuakeC on top of engine acceleration. Its source `.map` loader also makes the lab and tests independent of a map toolchain.

The fork keeps those mechanisms in the engine. The engine contains no trigger classname, point radius, linked-target lookup, priority or field list. These are game rules. Smoothing is a QuakeC policy that updates the engine's direction and strength each server physics frame; QuakeC never integrates position or velocity for gravity. This also makes a different game or field rule possible without rebuilding the engine.

## Added engine changes

- `client/escher_view.h`, `client/cl_input.c`, `client/client.h`: parallel-transport gaze from the previous gravity frame into the current one before applying mouse input. This preserves local pitch and heading during wall and ceiling turns, including 180-degree reversals. FTE's surface-relative pitch limits and mouse look remain in charge of input. The previous frame is per local player, not global.
- `client/cl_parse.c`: reset that transport history when the server explicitly fixes the view, so respawns and teleports do not apply an old frame to a freshly specified view.
- `client/view.c`: trace the gravity-relative eye offset to keep the camera out of walls where the old hull is narrower than the view offset.
- `common/pmove.c`: use the local gravity component when removing low-speed drift; sample edge friction below the player along local down. This code runs on both sides of prediction.
- `server/pr_cmds.c`: the generic `aim` builtin uses the entity's local vertical for its eye trace and vertical aim correction.

The native camera test includes the actual header used by the engine, rather than a rewritten mathematical model.

## Added game changes

`Escher_Frame` runs from `StartFrame`, evaluates the fields at each eligible entity's origin, and updates `.gravitydir` and `.gravity`. The broad policy currently includes WALK, STEP, TOSS, BOUNCE and FLYMISSILE entities. FLYMISSILE receives the local frame but deliberately retains the engine's non-ballistic flight semantics; ordinary rockets do not become falling objects. Static geometry, pushers, noclip and cosmetic sprites are not physically reoriented. To make another class ballistic, choose an appropriate engine movetype in that class's QuakeC.

Field entry captures the entity's previous direction/multiplier. Higher `priority` wins; equal values choose the most recently spawned field. Selection is deterministic and does not depend on distance. Disabling or leaving the final applicable field restores the captured state over 0.5 seconds. Momentum remains in world space, so changing up does not manufacture velocity. The entity can fall naturally onto its new surface. Ground state is cleared while direction changes sufficiently.

Direction follows a great-circle path at a bounded angular speed. An exact antipodal change chooses a deterministic perpendicular axis; it never interpolates through the zero vector. Strength eases exponentially toward the winning field's absolute acceleration. At a point/segment center the field has no defined radial direction and is skipped; it does not invent a direction or emit a NaN. A zero-length segment behaves as a point. Endpoints are resolved by targetname at evaluation time, so moving endpoints work.

Brush membership uses an engine `MOVE_OTHERONLY` point trace against the actual brush model, not its world AABB. Selection is based on entity origins, not merely overlapping bounds; boundary behavior is consequently independent of the object's size.

Jump and landing speed checks use local up. Muzzle origins and grenade launch arcs do too. Respawn clears inherited field state. None of those gameplay choices are hard-coded into the engine.

## Practical limits and next engine work

1. **Collision bodies do not rotate.** A Quake standing box is still tall along world Z, even on a wall. The eye collision trace prevents seeing through walls but does not fix body clearance. True gravity-oriented capsules/OBBs require a consistent shape sweep in server movement, player prediction, entity broadphase/linking and model collision. Q1's pre-expanded hulls cannot exactly support arbitrary orientations; a brush-based collision representation or another explicit approximation is needed. Replacing only the server hull would introduce prediction errors. This is the largest remaining engine task.
2. **Prediction uses the last received gravity.** Field selection is authoritative server game code; the client predicts movement using the transmitted result. FTE packs direction into two angular bytes and sends strength through its existing entity-gravity update. Fast turns and high-latency multiplayer can show corrections. A production multiplayer version should replicate field definitions/transition state and run identical selection and easing in predicted game code, or extend the state protocol with sufficient precision. Do not place map-specific radius logic in core `pmove`.
3. **Monster behavior remains conventional Quake.** Physics can pull STEP actors sideways, but yaw-only chase logic, melee checks, jump attacks, navigation and model presentation are not fully converted. Monster surface navigation is game work; supporting oriented movement/collision is engine work. The existing FTE basemod is itself experimental; this is not a claim of a fully converted campaign.
4. **Liquids, ladders, moving platforms and scripted moves need further audit.** FTE still marks some water/ladder paths with gravity TODOs. Legacy QuakeC doors, teleports and cinematics can deliberately impose world-space motion/angles. Those should be handled in their respective owning layer, not silently changed by a field.
5. **Zero strength uses a positive multiplier of 1e-9.** FTE reserves scalar zero for default gravity. This gives effectively zero acceleration without breaking existing Quake compatibility. An exact-zero mode should be an explicit engine API/protocol extension rather than changing the meaning of `.gravity == 0` globally.
6. **Selection is O(movable entities × fields)** per server physics tick. This is appropriate for a small foundation. If profiling shows a need, spatially index fields in game code and retain the same overlap semantics. Removing active fields directly from QuakeC is unsupported; toggle them instead, or implement list removal before deleting.

## Verification

`make test` compiles the real game plus a test worldspawn and runs it in the dedicated engine. It covers segment projection/caps/degeneracy, normalized antipodal transitions, angular speed, attraction/repulsion signs, absolute and near-zero strength, priority changes, restoration, toggles, undefined point centers, real brush membership, and actual sideways toss integration. Native tests check gaze transport, reversibility, full reversals and accumulated small-step precision. The suite fails on a missing pass marker, explicit failures, load failures or timeout. A successful automated run does not establish subjective comfort, full campaign compatibility or multiplayer correctness.
