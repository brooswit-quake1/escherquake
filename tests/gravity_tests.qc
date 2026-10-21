void(float ok, string label) check;
float checks;
entity physics_probe;

void() CheckZeroPhysics =
{
 check(vlen(physics_probe.velocity)<0.001, "near-zero gravity does not use default");
 dprint("ESCHER TESTS PASSED: "); dprint(ftos(checks)); dprint("\n");
 localcmd("quit\n");
};

void() CheckTossPhysics =
{
 check(physics_probe.velocity_x < -10, "engine toss accelerates along gravitydir");
 check(fabs(physics_probe.velocity_y)<0.001 && fabs(physics_probe.velocity_z)<0.001, "no world Z acceleration");
 check(physics_probe.origin_x < -0.1, "engine integrates sideways position");
 physics_probe.velocity='0 0 0'; physics_probe.gravity=0.000000001;
 self.think=CheckZeroPhysics; self.nextthink=time+0.15;
};

void(float ok, string label) check =
{
 if (!ok) { dprint("FAIL: "); dprint(label); dprint("\n"); localcmd("quit\n"); error("gravity regression"); }
 checks = checks + 1;
};
void() RunTests =
{
 local vector v;
 local entity e, f, endpoint, high;
 local float i;
 v = Escher_Closest('5 4 0', '0 0 0', '10 0 0');
 check(vlen(v-'5 0 0') < 0.001, "segment interior");
 check(Escher_Closest('-4 0 0','0 0 0','10 0 0') == '0 0 0', "segment start cap");
 check(Escher_Closest('20 0 0','0 0 0','10 0 0') == '10 0 0', "segment end cap");
 check(Escher_Closest('2 3 4','1 1 1','1 1 1') == '1 1 1', "degenerate segment");
 v = '0 0 -1';
 for (i=0; i<100; i=i+1)
 {
  v = Escher_Turn(v, '0 0 1', 0.04);
  check(fabs(vlen(v)-1)<0.0001, "antipodal unit vector");
 }
 check(vlen(v-'0 0 1') < 0.001, "ceiling transition completes");
 v = Escher_Turn('0 0 -1','1 0 0',0.1);
 check(fabs(v_x-sin(0.1))<0.0001, "angular rate");
 e = spawn(); e.movetype = MOVETYPE_TOSS; e.gravity = 0.5;
 setorigin(e,'100 0 0');
 f = spawn(); self=f; info_gravity();
 f.gravity_transition=-1; f.gravity_strength=400;
 Escher_Update(e,0.01);
 check(e.gravitydir == '-1 0 0', "point up away, acceleration toward");
 check(fabs(e.gravity-0.5)<0.001, "absolute strength");
 f.gravity_inward=1; Escher_Update(e,0.01);
 check(e.gravitydir == '1 0 0', "point up toward, acceleration away");
 f.gravity_zero=1; Escher_Update(e,0.01);
 check(e.gravity>0 && e.gravity<0.00001, "zero strength sentinel");
 f.gravity_zero=0; f.gravity_inward=0;
 high=spawn(); self=high; info_gravity(); high.gravity_transition=-1;
 high.priority=5; high.gravity_inward=1; f.priority=4;
 Escher_Update(e,0.01); check(e.gravitydir=='1 0 0', "higher point priority wins");
 f.priority=6; Escher_Update(e,0.01); check(e.gravitydir=='-1 0 0', "priority edits switch winner");
 f.priority=5; Escher_Update(e,0.01); check(e.gravitydir=='1 0 0', "equal priority newest field wins");
 f.priority=-10000000; high.priority=-10000001;
 Escher_Update(e,0.01); check(e.gravitydir=='-1 0 0', "very negative priorities work");
 high.gravity_disabled=1;
 endpoint=spawn(); endpoint.targetname="end"; setorigin(endpoint,'200 0 0');
 f.target="end"; setorigin(e,'100 50 0'); Escher_Update(e,0.01);
 check(e.gravitydir == '0 -1 0', "linked segment direction");
 setorigin(e,'2000 0 0');
 for(i=0;i<200;i=i+1) Escher_Update(e,0.01);
 check(e.gravitydir == '0 0 -1' && e.gravity==0.5 && !e.escher_active, "exit restores prior gravity");
 setorigin(e,'100 0 0'); f.target=""; f.gravity_disabled=1;
 Escher_Update(e,0.01);
 check(!e.escher_active,"disabled field ignored");
 f.gravity_disabled=0; setorigin(e,'0 0 0'); Escher_Update(e,0.01);
 check(!e.escher_active,"point center has no undefined direction");
 // Brush membership and cross-type priority tested against engine BSP collision.
 f = find(world, classname, "trigger_gravity");
 check(f != world, "brush fixture exists");
 if (f != world)
 {
  setorigin(e,'384 0 64'); check(Escher_Inside(f,e),"brush inside");
  setorigin(e,'800 0 64'); check(!Escher_Inside(f,e),"brush outside");
  setorigin(e,'400 0 64');
  setorigin(high,'384 0 64'); high.priority=20; high.gravity_disabled=0; high.gravity_inward=0;
  Escher_Update(e,0.01); check(e.gravitydir=='-1 0 0', "point overrides brush by priority");
  f.priority=30; f.gravity_transition=-1;
  Escher_Update(e,0.01); check(e.gravitydir=='1 0 0', "brush priority direction");
  check(fabs(e.gravity-0.75)<0.001, "brush overrides point strength");
  high.gravity_disabled=1;
 }
 f=find(world,targetname,"wedge_test"); check(f!=world,"wedge fixture exists");
 setorigin(e,'760 360 64'); check(Escher_Inside(f,e),"wedge inside");
 setorigin(e,'940 460 64'); check(!Escher_Inside(f,e),"wedge rejects empty portion of AABB");
 escher_fields=world;
 physics_probe=spawn(); physics_probe.movetype=MOVETYPE_TOSS;
 physics_probe.solid=SOLID_BBOX; setsize(physics_probe,'-8 -8 -8','8 8 8');
 setorigin(physics_probe,'0 0 256'); physics_probe.gravitydir='-1 0 0'; physics_probe.gravity=0.5;
 self=spawn(); self.think=CheckTossPhysics; self.nextthink=time+0.15;
};
void() worldspawn = { local entity runner; runner=spawn(); runner.think=RunTests; runner.nextthink=time+0.1; };
