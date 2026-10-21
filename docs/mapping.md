# Mapping and QuakeC API

`game/escher.fgd` supplies gravity entity definitions for editors such as TrenchBroom. Include it alongside your standard Quake definitions.

## Entities

`trigger_gravity` is a brush entity. `gravity_up` is its world-space up vector; for example `"-1 0 0"` makes gravity pull along +X. Membership tests the affected entity's origin inside the brush. This avoids the broad bounding-box-only behavior of ordinary trigger touch callbacks.

`info_gravity` is a point entity. Up defaults to **away from** its origin, so gravity attracts toward it. Set `gravity_inward` to `1` to make up point **toward** it, which repels objects. These names refer to **up**, not acceleration.

Give an `info_gravity` a `target` naming another point's `targetname` to make a finite segment. Use `info_gravity_target` for an endpoint that has no field of its own, or another `info_gravity` for a chain. Each segment uses the source point's settings. The closest point is clamped to the finite segment, so influence has spherical end caps. A missing endpoint falls back to a point field; coincident endpoints do too. Chains need no recursion and may form loops.

## Shared keys

| Key | Default | Meaning |
| --- | --- | --- |
| `priority` | `0` | Higher values override lower values across both brush and point/line fields. Negative values work. Equal values keep the most recently spawned field. |
| `gravity_strength` | `800` | Absolute acceleration in Quake units/s²; not a multiplier. Negative values clamp to zero. |
| `gravity_zero` | `0` | Set to `1` for effectively zero acceleration. Separate key distinguishes zero from an omitted strength. |
| `gravity_transition` | `0.75` | Seconds for a 180° direction change. Smaller angles take proportionally less time. Strength settles 99% over this time. Set negative for instantaneous changes; omitted/zero selects the default. |
| `gravity_disabled` | `0` | Initially disabled if nonzero. Normal target/use toggles it. |
| `targetname` | empty | Allows a button or trigger to toggle this field. |

Point/line keys: `gravity_radius` defaults to `256`; `gravity_inward` defaults to `0`; `target` optionally names the next point.

Keep `sv_gravity` positive (normally 800). Fields express absolute strengths through FTE's per-entity multiplier; a globally disabled `sv_gravity` also disables those forces. Use `gravity_zero` on a field for a local zero-gravity region.

Brush key: `gravity_up` defaults to `0 0 1` and is normalized on spawn.

Fields apply independently to movable entities, not just players. Overlaps choose one complete field, including its direction, strength and transition duration. They do not add forces or average directions. Leaving every field smoothly restores the entity's pre-field gravity over 0.5 seconds. Moving through a lower-priority field cannot reset a higher-priority field.

## Example

```text
{
"classname" "info_gravity"
"origin" "0 0 256"
"target" "rail_end"
"gravity_radius" "384"
"gravity_strength" "600"
"gravity_transition" "1.2"
"priority" "20"
}
{
"classname" "info_gravity_target"
"targetname" "rail_end"
"origin" "512 0 256"
}
```

An object at `256 100 256` gets up along +Y and acceleration along -Y. Setting `gravity_inward` to `1` reverses both. An object beyond either endpoint uses that endpoint as the closest point.

## API

FTE's normalized `.gravitydir` points **down**. Zero direction means default world down. `.gravity` is the multiplier of `sv_gravity`; zero multiplier also means the default. Use `Escher_Down(e)` to read an unambiguous unit vector.

`Escher_Apply(e, up, absolute_strength, transition_seconds, dt)` updates direction and magnitude once, and should be called each physics tick by a custom controller if you use it outside the field system. Initialize `e.escher_strength` to its current acceleration before starting such a custom controller. A nonpositive transition is instant at this API level. Do not simultaneously run a custom controller and the field controller on the same entity. For a one-shot immediate change:

```c
e.escher_strength = 600;
Escher_Apply(e, '0 1 0', 600, 0, frametime);
```

Direct `.gravitydir`/`.gravity` changes outside the field system are also supported by the engine. When a field takes control, its captured baseline includes those values. Clearing a field's `gravity_disabled` or calling its `use` callback is preferable to removing it, since the field registry is a linked list.

`Escher_Closest(p, a, b)` and `Escher_Turn(old_down, goal_down, max_radians)` are independently testable helpers. `Escher_Frame()` belongs in the server's `StartFrame`; it is already wired in this game.
