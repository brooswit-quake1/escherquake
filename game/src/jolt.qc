// Game-side examples only. Collision, integration, traces and lifecycle are engine work.
.float geomtype, mass, friction, damp_linear, damp_angular, max_angular, alloweddof, jointtype;

void() misc_jolt_box =
{
 precache_model("progs/jolt_box.mdl");
 setmodel(self,"progs/jolt_box.mdl");
 setsize(self,'-16 -16 -16','16 16 16');
 self.movetype=32; self.solid=32; self.geomtype=1;
 if (!self.mass) self.mass=5;
 if (!self.friction) self.friction=0.6;
 self.damp_linear=0.05; self.damp_angular=0.1;
};

void() misc_jolt_capsule =
{
 precache_model("progs/jolt_capsule.mdl");
 setmodel(self,"progs/jolt_capsule.mdl");
 setsize(self,'-16 -16 -24','16 16 32');
 self.movetype=32; self.solid=34; self.geomtype=3;
 if (!self.mass) self.mass=5;
 if (!self.friction) self.friction=0.6;
 self.damp_linear=0.05; self.damp_angular=0.1;
};
