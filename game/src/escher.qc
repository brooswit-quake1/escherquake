// EscherQuake. GPL-2.0-or-later. Gravity directions are DOWN; editor vectors are UP.
.vector gravitydir;
.vector gravity_up;
.float gravity_strength; // absolute acceleration, Quake units/sec^2
.float gravity_zero;
.float gravity_radius;
.float gravity_transition; // seconds for a half turn; strength settles to 99% in this time
.float priority;
.float gravity_inward; // up points toward source, therefore gravity points away
.float gravity_disabled;
.float gravity_kind; // 1 directional brush, 2 point/segment
.float escher_active;
.float escher_strength;
.float escher_basegravity;
.vector escher_basedown;
.entity escher_next;
var entity escher_fields;

float(float x) sqrt = #0;
float(float x) sin = #0;
float(float x) cos = #0;
float(float y, float x) atan2 = #0;
float(float x, float y) pow = #0;
void(vector start, vector mins, vector maxs, vector end, float flags, entity ignore) tracebox = #0;

vector(vector a, vector b) Escher_Cross =
{
 return [a_y*b_z-a_z*b_y, a_z*b_x-a_x*b_z, a_x*b_y-a_y*b_x];
};

vector(entity e) Escher_Down =
{
 if (e.gravitydir == '0 0 0') return '0 0 -1';
 return normalize(e.gravitydir);
};

// Great-circle interpolation, including exact 180-degree changes. Never lerp through zero.
vector(vector old, vector goal, float radians) Escher_Turn =
{
 local vector axis;
 local float angle, dot;
 old = normalize(old); goal = normalize(goal);
 dot = old * goal;
 if (dot > 1) dot = 1;
 if (dot < -1) dot = -1;
 axis = Escher_Cross(old, goal);
 angle = atan2(vlen(axis), dot);
 if (angle <= radians) return goal;
 if (vlen(axis) < 0.00001)
 {
  if (fabs(old_x) < 0.9) axis = Escher_Cross(old, '1 0 0');
  else axis = Escher_Cross(old, '0 1 0');
 }
 axis = normalize(axis);
 return normalize(old * cos(radians) + Escher_Cross(axis, old) * sin(radians));
};

vector(vector p, vector a, vector b) Escher_Closest =
{
 local vector ab;
 local float t, denom;
 ab = b - a; denom = ab * ab;
 if (denom < 0.000001) return a;
 t = ((p - a) * ab) / denom;
 if (t < 0) t = 0;
 if (t > 1) t = 1;
 return a + ab * t;
};

// Uses BSP collision rather than the brush's AABB: wedge and compound triggers work.
float(entity field, entity e) Escher_Inside =
{
 local entity saved;
 local float inside;
 saved = other; other = field;
 tracebox(e.origin, '0 0 0', '0 0 0', e.origin, 256, e);
 inside = trace_startsolid;
 other = saved;
 return inside;
};

// Public API for game code. Preserve world-space momentum; do not inject rotation energy.
void(entity e, vector up, float strength, float transition, float dt) Escher_Apply =
{
 local vector down, old;
 local float alpha, worldstrength;
 if (vlen(up) < 0.00001 || dt <= 0) return;
 down = normalize(0 - up); old = Escher_Down(e);
 if (transition <= 0) { e.gravitydir = down; alpha = 1; }
 else
 {
  e.gravitydir = Escher_Turn(old, down, 3.141592654 * dt / transition);
  alpha = 1 - pow(0.01, dt / transition);
 }
 if (strength < 0) strength = 0;
 e.escher_strength = e.escher_strength + (strength - e.escher_strength) * alpha;
 worldstrength = cvar("sv_gravity");
 if (worldstrength <= 0) worldstrength = 800;
 // FTE reserves a zero multiplier for default gravity. A tiny positive value avoids that sentinel.
 e.gravity = e.escher_strength / worldstrength;
 if (e.gravity < 0.000000001) e.gravity = 0.000000001;
 if (vlen(old - e.gravitydir) > 0.000001) e.flags = e.flags - (e.flags & FL_ONGROUND);
};

void(entity e, float dt) Escher_Update =
{
 local entity f, best, endpoint;
 local vector up, bestup, nearest;
 local float strength, winning_priority, distance;
 best = world; winning_priority = -999999;
 f = escher_fields;
 while (f != world)
 {
  if (!f.gravity_disabled)
  {
   up = '0 0 0';
   if (f.gravity_kind == 1)
   {
    if (Escher_Inside(f, e)) up = f.gravity_up;
   }
   else
   {
    nearest = f.origin;
    if (f.target != "")
    {
     endpoint = find(world, targetname, f.target);
     if (endpoint != world) nearest = Escher_Closest(e.origin, f.origin, endpoint.origin);
    }
    up = e.origin - nearest; distance = vlen(up);
    if (distance > f.gravity_radius || distance < 0.00001) up = '0 0 0';
    if (f.gravity_inward) up = 0 - up;
   }
   // Higher priority wins; equal priorities keep the most recently spawned field.
   if (up != '0 0 0' && (best == world || f.priority > winning_priority))
   { best = f; bestup = up; winning_priority = f.priority; }
  }
  f = f.escher_next;
 }
 if (best != world)
 {
  if (!e.escher_active)
  {
   e.escher_basegravity = e.gravity;
   e.escher_basedown = Escher_Down(e);
   strength = e.gravity; if (!strength) strength = 1;
   e.escher_strength = strength * cvar("sv_gravity");
   e.escher_active = 1;
  }
  strength = best.gravity_strength;
  if (best.gravity_zero) strength = 0;
  Escher_Apply(e, bestup, strength, best.gravity_transition, dt);
 }
 else if (e.escher_active)
 {
  strength = e.escher_basegravity; if (!strength) strength = 1;
  Escher_Apply(e, 0 - e.escher_basedown, strength * cvar("sv_gravity"), 0.5, dt);
  if (Escher_Down(e) * e.escher_basedown > 0.99999 && fabs(e.escher_strength - strength*cvar("sv_gravity")) < 0.01)
  {
   e.gravitydir = e.escher_basedown;
   e.gravity = e.escher_basegravity;
   e.escher_active = 0;
  }
 }
};

void() Escher_Frame =
{
 local entity e;
 if (escher_fields == world) return;
 e = nextent(world);
 while (e != world)
 {
  // Includes players, monsters, pickups, gibs, grenades and projectiles. Excludes pushers/noclip.
  if (!e.gravity_kind && (e.movetype == MOVETYPE_WALK || e.movetype == MOVETYPE_STEP ||
      e.movetype == MOVETYPE_TOSS || e.movetype == MOVETYPE_BOUNCE || e.movetype == MOVETYPE_FLYMISSILE || e.movetype == 32))
   Escher_Update(e, frametime);
  e = nextent(e);
 }
};

void() Escher_Toggle = { self.gravity_disabled = !self.gravity_disabled; };

void() Escher_Register =
{
 if (!self.gravity_strength) self.gravity_strength = 800;
 if (!self.gravity_transition) self.gravity_transition = 0.75;
 if (self.gravity_transition < 0) self.gravity_transition = 0; // negative mapper value = instant
 self.escher_next = escher_fields; escher_fields = self;
 self.use = Escher_Toggle;
};

// Brush trigger. gravity_up is a world-space up vector, e.g. "1 0 0" for a wall.
void() trigger_gravity =
{
 self.gravity_kind = 1;
 if (self.gravity_up == '0 0 0') self.gravity_up = '0 0 1';
 self.gravity_up = normalize(self.gravity_up);
 setmodel(self, self.model);
 // Keep the collision model index; an empty model string suppresses networking.
 self.model = "";
 self.solid = 22; self.movetype = MOVETYPE_NONE; // FTE SOLID_BSPTRIGGER
 Escher_Register();
};

// Point, or capsule around a finite segment when target names an endpoint.
void() info_gravity =
{
 self.gravity_kind = 2;
 self.solid = SOLID_NOT; self.movetype = MOVETYPE_NONE;
 if (self.gravity_radius <= 0) self.gravity_radius = 256;
 Escher_Register();
};

void() info_gravity_target = { self.solid = SOLID_NOT; self.movetype = MOVETYPE_NONE; };
