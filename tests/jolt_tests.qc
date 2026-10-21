void() jolt_inline_fixture = { self.solid=SOLID_NOT; };
float jolt_clock;
void() StartFrame = { jolt_clock=jolt_clock+frametime; };
void() ClientDisconnect = {};
entity() spawnclient = #454;
void() PlayerPreThink = {};
void() PlayerPostThink = {};
.float dimension_hit, dimension_solid;
entity falling, sideways, capsule, fixed_body, joint, concave, asymmetric, xc, yc, cylinder, fast;
float checks, starttime, stage, floor_touches;
entity mass_probe, mass_joint;
void() JoltTouch = { if (other==world) floor_touches=floor_touches+1; };
void(float ok, string label) jcheck =
{
 if (!ok) { dprint("JOLT FAIL: "); dprint(label); dprint("\n"); localcmd("quit\n"); error("Jolt regression"); }
 checks = checks + 1;
};
entity(vector position, vector lo, vector hi, float shape) body =
{
 local entity e;
 e=spawn(); e.classname="jolt_probe"; e.movetype=32; e.solid=32;
 e.geomtype=shape; e.mass=1; e.dimension_solid=1; e.dimension_hit=1;
 e.gravity=0.000000001;
 setsize(e,lo,hi); setorigin(e,position);
 return e;
};
void() JoltCheck;
void() JoltRemoved;
void() JoltFloor;
void() JoltResumed;
void() JoltSetup =
{
 falling=body('-160 0 100','-8 -8 -8','8 8 8',1); falling.gravity=1; falling.touch=JoltTouch;
 sideways=body('-100 0 150','-4 -4 -4','4 4 4',1); sideways.gravity=0.5; sideways.gravitydir='1 0 0';
 capsule=body('40 0 100','-8 -8 -24','8 8 24',3); capsule.angles='90 0 0'; capsule.alloweddof=7;
 fixed_body=body('130 0 100','-8 -8 -8','8 8 8',1);
 joint=spawn(); joint.jointtype=-1; joint.enemy=fixed_body; setorigin(joint,'130 0 100');
 concave=body('0 140 160','-1 -1 -1','1 1 1',4); setmodel(concave,"*1"); concave.angles='0 90 0';
 asymmetric=body('120 -80 100','-8 -8 -16','8 8 32',3);
 xc=body('-100 -100 120','-24 -8 -8','24 8 8',6);
 yc=body('-20 -100 120','-8 -24 -8','8 24 8',7);
 cylinder=body('180 -80 100','-8 -8 -24','8 8 24',5);
 fast=body('-200 200 80','-4 -4 -4','4 4 4',1); fast.velocity='0 0 -10000';
 mass_probe=body('180 0 160','-4 -4 -4','4 4 4',1);
 mass_joint=spawn(); mass_joint.jointtype=-1; mass_joint.enemy=mass_probe; setorigin(mass_joint,'180 0 160');
 starttime=jolt_clock; self.think=JoltCheck; self.nextthink=time+0.2;
};
void() JoltCheck =
{
 local float dt;
 dt=jolt_clock-starttime;
 if (dt < 0.2) { self.nextthink=time+0.001; return; }
 jcheck(fabs(falling.velocity_z+800*dt)<30, "elapsed-time gravity acceleration");
 jcheck(sideways.velocity_x > 40 && fabs(sideways.velocity_z)<0.01, "per-body sideways gravity");
 jcheck(fabs(capsule.origin_x-40)<0.1 && fabs(capsule.origin_z-100)<0.1, "rotated capsule origin stable");
 traceline('0 0 100','80 0 100',FALSE,world);
 jcheck(trace_ent==capsule, "traceline hits rotated capsule");
 jcheck(fabs(trace_endpos_x-16)<0.15, "capsule full length and rotated local Z");
 traceline('40 0 120','40 0 80',FALSE,world);
 jcheck(trace_ent==capsule && fabs(trace_endpos_z-108)<0.15, "capsule radius after rotation");
 tracebox('0 0 100','-4 -4 -4','4 4 4','80 0 100',FALSE,world);
 jcheck(trace_ent==capsule && trace_endpos_x < 13 && trace_endpos_x > 11, "tracebox uses bounds against actual shape");
 traceline('0 0 100','80 0 100',FALSE,capsule);
 jcheck(trace_fraction==1, "trace ignores its own body");
 traceline('40 0 100','40 0 100',FALSE,world);
 jcheck(trace_startsolid && trace_allsolid, "stationary trace inside body");
 traceline('40 0 100','80 0 100',FALSE,world);
 jcheck(trace_startsolid && !trace_allsolid, "trace can escape solid start");
 traceline('-32 172 200','-32 172 120',FALSE,world);
 jcheck(trace_fraction==1, "rotated concave inline BSP preserves empty corner");
 traceline('-32 92 200','-32 92 120',FALSE,world);
 jcheck(trace_ent==concave && fabs(trace_endpos_z-168)<0.15, "rotated inline BSP solid arm");
 traceline('120 -80 150','120 -80 70',FALSE,world);
 jcheck(trace_ent==asymmetric && fabs(trace_endpos_z-132)<0.15, "asymmetric bounds preserve QC origin");
 traceline('-140 -100 120','-60 -100 120',FALSE,world);
 jcheck(trace_ent==xc && fabs(trace_endpos_x+124)<0.15, "explicit capsule X axis");
 traceline('-20 -140 120','-20 -60 120',FALSE,world);
 jcheck(trace_ent==yc && fabs(trace_endpos_y+124)<0.15, "explicit capsule Y axis");
 traceline('185 -80 150','185 -80 70',FALSE,world);
 jcheck(trace_ent==cylinder && fabs(trace_endpos_z-124)<0.15, "cylinder flat end and full height");
 jcheck(vlen(fixed_body.origin-'130 0 100')<0.1, "fixed joint world anchor");
 jcheck(capsule.absmin_x < 16 && capsule.absmax_x > 64, "rotated capsule bounds reach both ends");
 capsule.dimension_solid=2;
 traceline('0 0 100','80 0 100',FALSE,sideways);
 jcheck(trace_fraction==1, "trace respects physics dimensions");
 capsule.dimension_solid=1;
 sideways.owner=capsule;
 traceline('0 0 100','80 0 100',FALSE,sideways);
 jcheck(trace_fraction==1, "trace respects owner exclusion");
 sideways.owner=world;
 mass_probe.mass=3;
 remove(fixed_body); remove(joint); // constraints must be removed before bodies
 remove(capsule);
 self.think=JoltRemoved; self.nextthink=time+0.1;
};
void() JoltRemoved =
{
 traceline('0 0 100','80 0 100',FALSE,world);
 jcheck(trace_fraction==1, "removed body leaves no collision");
 jcheck(vlen(mass_probe.origin-'180 0 160')<0.1, "mass edit rebuilds body and attached constraint safely");
 self.think=JoltFloor; self.nextthink=time+0.65;
};
void() JoltFloor =
{
 if (jolt_clock-starttime < 1.05) { self.nextthink=time+0.001; return; }
 jcheck(falling.origin_z > 7.5 && falling.origin_z < 9, "Q1 BSP floor supports box");
 jcheck(fabs(falling.velocity_z)<2, "box settles on Q1 collision geometry");
 jcheck(floor_touches>0, "physics contacts call QuakeC touch on main thread");
 jcheck(fast.origin_z > 3.5 && fast.origin_z < 5, "CCD stops fast body at thin Q1 floor");
 dprint("JOLT TESTS PASSED: "); dprint(ftos(checks)); dprint("\n");
 if (cvar("jolt_reload_marker") == 0)
 {
  self.think=JoltResumed; self.nextthink=time+0.15;
  localcmd("save jolt_phase1\nload jolt_phase1\n");
 }
 else localcmd("quit\n");
};
void() JoltResumed =
{
 jcheck(falling != world && falling.origin_z > 7.5 && falling.origin_z < 9, "save/load rebuilds physics bodies");
 dprint("JOLT SAVELOAD PASSED\n");
 localcmd("set jolt_reload_marker 1\nmap jolt_room\n");
};
void() worldspawn =
{
 local entity runner, driver;
 driver=spawnclient(); driver.netname="physics test driver"; driver.health=100; driver.movetype=MOVETYPE_NOCLIP; driver.solid=SOLID_NOT;
 precache_model("*1");
 runner=spawn(); runner.think=JoltSetup; runner.nextthink=time+0.05;
};
