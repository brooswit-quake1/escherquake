/*
JOLT PHYSICS PLUGIN FOR FTEQW
written by erysdren (it/its)
started work on May 14 2025
*/

/*
Phase-one stabilization continues FTE PR 414. Core timing, tracing, shape
ownership, capsule axes/dimensions, BSP compounds, per-body gravity, bounds,
and save/load are covered by the engine integration tests.

Remaining feature work: ragdolls, buoyancy, simulated particles, debug drawing,
QC-authored hulls/compounds, universal/hinge2 joints and extended joint types.
See docs/jolt.md in EscherQuake for scope, provenance and known limits.
*/

/*
# CHANGELOG
## last updated 2025-06-08
- Added Jolt Physics as both a standalone and internal plugin
- server/progdefs.h: added missing physics fields to entityrbe_t
- server/progdefs.h: changed gravity field in entityrbe_t from a qboolean to a float
- server/progdefs.h: added docstrings to the various DP/FTE entity physics fields
- server/world.c: re-enabled the use of the RBE trace callback in World_Move()
- added EnumerateBrushes callback for models so a brush list can be gathered
- added .alloweddof entity field (with corresponding constants) to limit the
movement and rotation of rigidbody entities
- common/console.h: fixed a whitespace warning that only affects C++
- engine/makefile: updated makelibs cmake invocation for web target
*/

#ifndef FTEPLUGIN
#define FTEENGINE
#define FTEPLUGIN
#define Plug_Init Plug_Jolt_Init
#endif

#include "quakedef.h"
#include "../plugin.h"
#include "../engine.h"

#include "pr_common.h"
#include "com_mesh.h"
#include "com_bih.h"

#include "Jolt/Jolt.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Core/QuickSort.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Physics/PhysicsSettings.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"
#include "Jolt/Physics/Collision/Shape/ConvexHullShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Constraints/TwoBodyConstraint.h"
#include <vector>
#include <array>
#include <algorithm>
#include <mutex>
#include "Jolt/Physics/Collision/ContactListener.h"
#include "Jolt/Physics/Collision/CollisionCollectorImpl.h"
#include "Jolt/Physics/Collision/ShapeCast.h"
#include <cmath>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "Jolt/Physics/Constraints/PointConstraint.h"
#include "Jolt/Physics/Constraints/FixedConstraint.h"
#include "Jolt/Physics/Constraints/SliderConstraint.h"
#include "Jolt/Physics/Constraints/HingeConstraint.h"

#ifndef FTEENGINE
#define BZ_Malloc malloc
#define BZ_Free free
int VectorCompare(const vec3_t v1, const vec3_t v2)
{
	for (int i = 0; i < 3; i++)
		if (v1[i] != v2[i])
			return 0;
	return 1;
}
vec_t Length(const vec3_t v)
{
	int		i;
	float	length;

	length = 0;
	for (i=0 ; i< 3 ; i++)
		length += v[i]*v[i];
	length = sqrt (length);		// FIXME

	return length;
}
float RadiusFromBounds(const vec3_t mins, const vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return Length (corner);
}
void CrossProduct (const vec3_t v1, const vec3_t v2, vec3_t cross)
{
	cross[0] = v1[1]*v2[2] - v1[2]*v2[1];
	cross[1] = v1[2]*v2[0] - v1[0]*v2[2];
	cross[2] = v1[0]*v2[1] - v1[1]*v2[0];
}
float QDECL VectorNormalize (vec3_t v)
{
	float	length;
	float	ilength;

	length = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
	length = sqrt (length);		// FIXME

	if (length)
	{
		ilength = 1.0/length;
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;
}
// common/q1bsp.c
#define MAXFRAGMENTVERTS 360
int Fragment_ClipPolyToPlane(float *inverts, float *outverts, int incount, float *plane, float planedist)
{
#define C (sizeof(vecV_t)/sizeof(vec_t))
	float dotv[MAXFRAGMENTVERTS+1];
	char keep[MAXFRAGMENTVERTS+1];
#define KEEP_KILL 0
#define KEEP_KEEP 1
#define KEEP_BORDER 2
	int i;
	int outcount = 0;
	int clippedcount = 0;
	float d, *p1, *p2, *out;
#define FRAG_EPSILON (1.0/32) //0.5

	for (i = 0; i < incount; i++)
	{
		dotv[i] = DotProduct((inverts+i*C), plane) - planedist;
		if (dotv[i]<-FRAG_EPSILON)
		{
			keep[i] = KEEP_KILL;
			clippedcount++;
		}
		else if (dotv[i] > FRAG_EPSILON)
			keep[i] = KEEP_KEEP;
		else
			keep[i] = KEEP_BORDER;
	}
	dotv[i] = dotv[0];
	keep[i] = keep[0];

	if (clippedcount == incount)
		return 0;	//all were clipped
	if (clippedcount == 0)
	{	//none were clipped
		for (i = 0; i < incount; i++)
			VectorCopy((inverts+i*C), (outverts+i*C));
		return incount;
	}

	for (i = 0; i < incount; i++)
	{
		p1 = inverts+i*C;
		if (keep[i] == KEEP_BORDER)
		{
			out = outverts+outcount++*C;
			VectorCopy(p1, out);
			continue;
		}
		if (keep[i] == KEEP_KEEP)
		{
			out = outverts+outcount++*C;
			VectorCopy(p1, out);
		}
		if (keep[i+1] == KEEP_BORDER || keep[i] == keep[i+1])
			continue;
		p2 = inverts+((i+1)%incount)*C;
		d = dotv[i] - dotv[i+1];
		if (d)
			d = dotv[i] / d;

		out = outverts+outcount++*C;
		VectorInterpolate(p1, d, p2, out);
	}
	return outcount;
}

//the plane itself must be a vec4_t, but can have other data packed between
size_t Fragment_ClipPlaneToBrush(vecV_t *points, size_t maxpoints, void *planes, size_t planestride, size_t numplanes, vec4_t face)
{
	int p, a;
	vecV_t verts[MAXFRAGMENTVERTS];
	vecV_t verts2[MAXFRAGMENTVERTS];
	vecV_t *cverts;
	int flip;
//	vec3_t d1, d2, n;
	size_t numverts;

	//generate some huge quad/poly aligned with the plane
	vec3_t tmp;
	vec3_t right, forward;
	double t;
	float *plane;

//	if (face[2] != 1)
//		return 0;

	t = fabs(face[2]);
	if (t > fabs(face[0]) && t > fabs(face[1]))
		VectorSet(tmp, 1, 0, 0);
	else
		VectorSet(tmp, 0, 0, 1);

	CrossProduct(face, tmp, right);
	VectorNormalize(right);
	CrossProduct(face, right, forward);
	VectorNormalize(forward);

	VectorScale(face, face[3],			verts[0]);
	VectorMA(verts[0], 32767, right,		verts[0]);
	VectorMA(verts[0], 32767, forward,	verts[0]);

	VectorScale(face, face[3],			verts[1]);
	VectorMA(verts[1], 32767, right,		verts[1]);
	VectorMA(verts[1], -32767, forward,	verts[1]);

	VectorScale(face, face[3],			verts[2]);
	VectorMA(verts[2], -32767, right,	verts[2]);
	VectorMA(verts[2], -32767, forward,	verts[2]);

	VectorScale(face, face[3],			verts[3]);
	VectorMA(verts[3], -32767, right,	verts[3]);
	VectorMA(verts[3], 32767, forward,	verts[3]);

	numverts = 4;


	//clip the quad to the various other planes
	flip = 0;
	for (p = 0; p < numplanes; p++)
	{
		plane = (float*)((qbyte*)planes + p*planestride);
		if (plane != face)
		{
			vec3_t norm;
			flip^=1;
			VectorNegate(plane, norm);
			if (flip)
				numverts = Fragment_ClipPolyToPlane((float*)verts, (float*)verts2, numverts, norm, -plane[3]);
			else
				numverts = Fragment_ClipPolyToPlane((float*)verts2, (float*)verts, numverts, norm, -plane[3]);

			if (numverts < 3)	//totally clipped.
				return 0;
		}
	}

	if (numverts > maxpoints)
		return 0;

	if (flip)
		cverts = verts2;
	else
		cverts = verts;
	for (p = 0; p < numverts; p++)
	{
		for (a = 0; a < 3; a++)
		{
			float f = cverts[p][a];
			int rounded = floor(f + 0.5);
			//if its within 1/1000th of a qu, just round it.
			if (fabs(f - rounded) < 0.001)
				points[p][a] = rounded;
			else
				points[p][a] = f;
		}
	}

	return numverts;
}
#endif

#ifndef DEG2RAD
#define DEG2RAD(d) (d * M_PI * (1/180.0f))
#endif

#ifndef RAD2DEG
#define RAD2DEG(d) ((d*180) / M_PI)
#endif

static plugthreadfuncs_t *threadfuncs = nullptr;
static rbeplugfuncs_t *rbefuncs = nullptr;
static cvar_t *physics_jolt_maxiterationsperframe = nullptr;
static cvar_t *physics_jolt_framerate = nullptr;
static cvar_t *pr_meshpitch = nullptr;

/* taken from VPhysics-Jolt */
static const float InchesToMeters = 0.0254f;
static const float MetersToInches = 1.0f / 0.0254f;

enum {
	LAYER_STATIC,
	LAYER_MOVING,
	NUM_LAYERS
};

typedef struct joltcontext {
	rigidbodyengine_t funcs;

	static const unsigned int max_bodies = 65536;
	static const unsigned int num_body_mutexes = 0;
	static const unsigned int max_body_pairs = 65536;
	static const unsigned int max_contact_constraints = 10240;

	static const unsigned int max_physics_jobs = JPH::cMaxPhysicsJobs;
	static const unsigned int max_physics_barriers = JPH::cMaxPhysicsBarriers;

	JPH::TempAllocatorMalloc temp_allocator;
	JPH::PhysicsSystem physics_system;
	JPH::JobSystemThreadPool job_system;

#if 0
	class JobSystemImpl : public JPH::JobSystemWithBarrier
	{
	public:
		JobSystemImpl() : JobSystemWithBarrier(max_physics_barriers) { }
		~JobSystemImpl() = default;

		virtual JobHandle CreateJob(const char *inName, JPH::ColorArg inColor, const JPH::JobSystem::JobFunction &inJobFunction, uint32_t inNumDependencies = 0)
		{

		}
	} job_system;
#endif

	class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
		{
			if (inObject1 == LAYER_STATIC)
				return inObject2 == LAYER_MOVING;
			else
				return inObject1 == LAYER_MOVING;
		}
	} object_vs_object_layer_filter;

	class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		virtual unsigned int GetNumBroadPhaseLayers() const override
		{
			return NUM_LAYERS;
		}
		virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
		{
			return (JPH::BroadPhaseLayer)inLayer;
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		virtual const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
		{
			switch (inLayer.GetValue())
			{
				case LAYER_STATIC: return "STATIC";
				case LAYER_MOVING: return "MOVING";
				default: return "INVALID";
			}
		}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED
	} broad_phase_layer_interface;

	class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
		{
			if (inLayer1 == LAYER_STATIC)
				return inLayer2 == (JPH::BroadPhaseLayer)LAYER_MOVING;
			else
				return inLayer1 == LAYER_MOVING;
		}
	} object_vs_broadphase_layer_filter;

	double accumulator = 0;
	struct ContactEvent
	{
		JPH::BodyID a, b;
		JPH::RVec3 point;
		JPH::Vec3 normal;
	};
	std::mutex contact_mutex;
	std::vector<ContactEvent> contacts;

	class ContactFilter : public JPH::ContactListener
	{
	public:
		world_t *world = nullptr;
		joltcontext *ctx = nullptr;
		JPH::ValidateResult OnContactValidate(const JPH::Body &a, const JPH::Body &b, JPH::RVec3Arg, const JPH::CollideShapeResult &) override
		{
			const auto na = a.GetUserData(), nb = b.GetUserData();
			if (na >= world->num_edicts || nb >= world->num_edicts)
				return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
			auto *ea = WEDICT_NUM_PB(world->progs, na);
			auto *eb = WEDICT_NUM_PB(world->progs, nb);
			if (!((int)ea->xv->dimension_solid & (int)eb->xv->dimension_hit) ||
				!((int)eb->xv->dimension_solid & (int)ea->xv->dimension_hit) ||
				(ea->v->owner && PROG_TO_WEDICT(world->progs, ea->v->owner) == eb) ||
				(eb->v->owner && PROG_TO_WEDICT(world->progs, eb->v->owner) == ea))
				return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}
		void OnContactAdded(const JPH::Body &a, const JPH::Body &b, const JPH::ContactManifold &manifold, JPH::ContactSettings &) override
		{
			std::lock_guard<std::mutex> guard(ctx->contact_mutex);
			if (ctx->contacts.size() < max_contact_constraints)
				ctx->contacts.push_back({a.GetID(), b.GetID(), manifold.GetWorldSpaceContactPointOn1(0), manifold.mWorldSpaceNormal});
		}
		void OnContactPersisted(const JPH::Body &a, const JPH::Body &b, const JPH::ContactManifold &manifold, JPH::ContactSettings &settings) override
		{
			OnContactAdded(a, b, manifold, settings);
		}

	} contact_filter;

	qboolean broadphase_optimized;

	rbecommandqueue_t *cmdqueuehead;
	rbecommandqueue_t *cmdqueuetail;
} joltcontext_t;

static void QDECL World_Jolt_End(world_t *world)
{
	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	while (ctx->cmdqueuehead)
	{
		rbecommandqueue_t *cmd = ctx->cmdqueuehead;
		ctx->cmdqueuehead = cmd->next;
		BZ_Free(cmd);
	}
	delete ctx;
	world->rbe = nullptr;
}

static void QDECL World_Jolt_RemoveJointFromEntity(world_t *world, wedict_t *ed)
{
	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	if (ed->rbe.joint.joint)
		 ctx->physics_system.RemoveConstraint((JPH::Constraint *)ed->rbe.joint.joint);
	ed->rbe.joint_type = 0;
	ed->rbe.joint.joint = nullptr;
}

static void QDECL World_Jolt_RemoveFromEntity(world_t *world, wedict_t *ed)
{
	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	JPH::BodyInterface &body_interface = ctx->physics_system.GetBodyInterface();
	JPH::Body *body;
	JPH::Shape *geom;

	// not physics controlled
	if (!ed->rbe.physics)
		return;

	body = (JPH::Body *)ed->rbe.body.body;
	ed->rbe.body.body = nullptr;
	if (body)
	{
// Remove joint entity references before Jolt destroys their bodies.
		for (unsigned int i = 0; i < world->num_edicts; ++i)
		{
			wedict_t *joint = WEDICT_NUM_PB(world->progs, i);
			if (joint->rbe.joint.joint)
			{
				auto *constraint = static_cast<JPH::TwoBodyConstraint *>(joint->rbe.joint.joint);
				if (constraint->GetBody1() == body || constraint->GetBody2() == body)
					World_Jolt_RemoveJointFromEntity(world, joint);
			}
		}

		// remove body
		body_interface.RemoveBody(body->GetID());
		body_interface.DestroyBody(body->GetID());
	}

	geom = (JPH::Shape *)ed->rbe.body.geom;
	ed->rbe.body.geom = nullptr;
	if (geom)
		geom->Release();

	// entity is not physics controlled, free any physics data
	rbefuncs->ReleaseCollisionMesh(ed);
	ed->rbe.massbuf = nullptr;
	ed->rbe.physics = qfalse;
}

static void World_Jolt_RunCommand(world_t *world, rbecommandqueue_t *cmd)
{
	if (!cmd->edict->rbe.body.body)
		return;

	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	JPH::BodyInterface &body_interface = ctx->physics_system.GetBodyInterface();
	JPH::Body *body = (JPH::Body *)cmd->edict->rbe.body.body;
	const JPH::BodyID &body_id = body->GetID();

	switch (cmd->command)
	{
		case RBECMD_ENABLE:
		{
			body_interface.ActivateBody(body_id);
			break;
		}

		case RBECMD_DISABLE:
		{
			body_interface.DeactivateBody(body_id);
			break;
		}

		case RBECMD_FORCE:
		{
			// JPH::RVec3 center = body_interface.GetCenterOfMassPosition(body_id);
			// JPH::RVec3 v2 = JPH::RVec3(cmd->v2[0], cmd->v2[1], cmd->v2[2]) * InchesToMeters;
			// JPH::RVec3 pos = v2 - center;

			body_interface.AddForce(body_id, JPH::Vec3Arg(cmd->v1[0], cmd->v1[1], cmd->v1[2]), JPH::RVec3(cmd->v2[0], cmd->v2[1], cmd->v2[2]) * InchesToMeters);
			break;
		}

		case RBECMD_TORQUE:
		{
			body_interface.AddTorque(body_id, JPH::Vec3Arg(cmd->v1[0], cmd->v1[1], cmd->v1[2]));
			break;
		}
	}
}

struct enumeratebrushes_userdata_t {
	world_t *world;
	model_t *model;
	wedict_t *ed;
	JPH::StaticCompoundShapeSettings static_compound_shape_settings;
	JPH::Array<JPH::ConvexHullShapeSettings *> convex_hull_shape_settings;

	enumeratebrushes_userdata_t() = default;
	~enumeratebrushes_userdata_t()
	{
		// Settings are reference counted by StaticCompoundShapeSettings.
		convex_hull_shape_settings.clear();
	}
};

static void QDECL EnumerateBrushes_Callback(model_t *model, q2cbrush_t *brush, void *user)
{
	enumeratebrushes_userdata_t *userdata = (enumeratebrushes_userdata_t *)user;

	if (!brush->numsides || !(brush->contents & FTECONTENTS_SOLID))
		return;

	JPH::ConvexHullShapeSettings *convex_hull_shape_settings = new JPH::ConvexHullShapeSettings;

	// copy planes into a linear array
	std::vector<std::array<float, 4>> planes(brush->numsides);
	for (int i = 0; i < brush->numsides; i++)
	{
		VectorCopy(brush->brushside[i].plane->normal, planes[i].data());
		planes[i][3] = brush->brushside[i].plane->dist;
	}

	// calculate points from the brushsides
	vecV_t facepoints[JPH::ConvexHullShape::cMaxPointsInHull];
	for (int i = 0; i < brush->numsides; i++)
	{
		int numpoints = Fragment_ClipPlaneToBrush(facepoints, countof(facepoints), planes.data(), sizeof(planes[0]), brush->numsides, planes[i].data());

		// add to convex hull settings
		for (int j = 0; j < numpoints; j++)
			convex_hull_shape_settings->mPoints.push_back(JPH::Vec3(facepoints[j][0], facepoints[j][1], facepoints[j][2]) * InchesToMeters * (userdata->ed->xv->scale ? userdata->ed->xv->scale : 1));
	}

	if (!convex_hull_shape_settings->mPoints.size())
	{
		delete convex_hull_shape_settings;
		return;
	}

	convex_hull_shape_settings->mMaxConvexRadius = 0;

	auto result = convex_hull_shape_settings->Create();
	if (result.HasError())
	{
		delete convex_hull_shape_settings;
		return; // BSP partitions can include zero-volume cells.
	}
	userdata->convex_hull_shape_settings.push_back(convex_hull_shape_settings);
	userdata->static_compound_shape_settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), convex_hull_shape_settings);
}

static void World_Jolt_BodyFromEntity(world_t *world, wedict_t *ed)
{
	JPH::Body *body = nullptr;
	JPH::Ref<JPH::Shape> geom;
	JPH::MeshShapeSettings mesh_shape_settings;
	JPH::ConvexHullShapeSettings convex_hull_shape_settings;
	enumeratebrushes_userdata_t enumeratebrushes_userdata;
	vec3_t maxs = {0, 0, 0};
	vec3_t mins = {0, 0, 0};
	vec3_t center;
	vec3_t size;
	vec3_t origin;
	vec3_t velocity;
	vec3_t angles;
	vec3_t avelocity;
	vec3_t spinvelocity;
	vec3_t forward, left, up;
	int modelindex = 0;
	int movetype = MOVETYPE_NONE;
	int solid = SOLID_NOT;
	int geomtype = GEOMTYPE_SOLID;
	vec_t scale = 1;
	vec_t mass = 1;
	model_t *model = nullptr;
	qboolean modified = qfalse;
	int numbrushes = 0;

	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	JPH::BodyInterface &body_interface = ctx->physics_system.GetBodyInterface();

	// FIXME: find a better way to prevent this crash?
	if (ed->v->movetype == MOVETYPE_NOCLIP)
	{
		World_Jolt_RemoveFromEntity(world, ed);
		return;
	}

	// get entity fields
	VectorCopy(ed->v->origin, origin);
	geomtype = ed->xv->geomtype;
	solid = ed->v->solid;
	if (solid == SOLID_NOT || solid == SOLID_TRIGGER || solid == SOLID_BSPTRIGGER || solid == SOLID_LADDER || solid == SOLID_PORTAL)
	{
		World_Jolt_RemoveFromEntity(world, ed);
		return;
	}
	movetype = ed->v->movetype;
	if (ed->xv->scale)
		scale = ed->xv->scale;

	// invalid geomtype, figure out what it should be based on solid type
	if (!geomtype)
	{
		switch (solid)
		{
			case SOLID_NOT:
				geomtype = GEOMTYPE_NONE;
				break;
			case SOLID_TRIGGER:
				geomtype = GEOMTYPE_NONE;
				break;
			case SOLID_BSP:
				geomtype = GEOMTYPE_TRIMESH;
				break;
			case SOLID_PHYSICS_TRIMESH:
				geomtype = GEOMTYPE_TRIMESH;
				break;
			case SOLID_PHYSICS_BOX:
				geomtype = GEOMTYPE_BOX;
				break;
			case SOLID_PHYSICS_SPHERE:
				geomtype = GEOMTYPE_SPHERE;
				break;
			case SOLID_PHYSICS_CAPSULE:
				geomtype = GEOMTYPE_CAPSULE;
				break;
			case SOLID_PHYSICS_CYLINDER:
				geomtype = GEOMTYPE_CYLINDER;
				break;
			default:
				geomtype = GEOMTYPE_BOX;
				break;
		}
	}

	// setup values based on geomtype
	switch (geomtype)
	{
		case GEOMTYPE_TRIMESH:
		{
			modelindex = ed->v->modelindex;
			model = world->Get_CModel(world, modelindex);
			if (!model || model->loadstate != MLS_LOADED)
			{
				model = nullptr;
				modelindex = 0;
			}
			if (model)
			{
				VectorScale(model->mins, scale, mins);
				VectorScale(model->maxs, scale, maxs);
				if (ed->xv->mass)
					mass = ed->xv->mass;
			}
			else
			{
				modelindex = 0;
				mass = 1.0f;
			}
			break;
		}

		case GEOMTYPE_BOX:
		case GEOMTYPE_SPHERE:
		case GEOMTYPE_CAPSULE:
		case GEOMTYPE_CAPSULE_X:
		case GEOMTYPE_CAPSULE_Y:
		case GEOMTYPE_CAPSULE_Z:
		case GEOMTYPE_CYLINDER:
		case GEOMTYPE_CYLINDER_X:
		case GEOMTYPE_CYLINDER_Y:
		case GEOMTYPE_CYLINDER_Z:
		{
			VectorCopy(ed->v->mins, mins);
			VectorCopy(ed->v->maxs, maxs);
			// VectorScale(ed->v->mins, scale, mins);
			// VectorScale(ed->v->maxs, scale, maxs);
			if (ed->xv->mass)
				mass = ed->xv->mass;
			break;
		}

		default:
		{
			World_Jolt_RemoveFromEntity(world, ed);
			return;
		}
	}

	// we don't allow point-size physics objects...
	VectorSubtract(maxs, mins, size);
	if (size[0] <= 0 || size[1] <= 0 || size[2] <= 0)
	{
		// Con_Printf("Jolt: entity %i (classname %s) is point sized\n", NUM_FOR_EDICT(world->progs, (edict_t *)ed), PR_GetString(world->progs, ed->v->classname));
		World_Jolt_RemoveFromEntity(world, ed);
		return;
	}

	// Rebuild on mass/motion changes too, removing dependent constraints first.
	mass = movetype == MOVETYPE_PHYSICS ? std::max(0.001f, mass) : 0;
	// check if we need to create or replace the geom
	if (!ed->rbe.physics || !VectorCompare(ed->rbe.mins, mins) || !VectorCompare(ed->rbe.maxs, maxs) || ed->rbe.modelindex != modelindex || ed->rbe.jolt_geomtype != geomtype || ed->rbe.jolt_movetype != movetype || ed->rbe.jolt_scale != scale || ed->rbe.mass != mass)
	{
		modified = qtrue;
		World_Jolt_RemoveFromEntity(world, ed);
		ed->rbe.physics = qtrue;
		VectorCopy(mins, ed->rbe.mins);
		VectorCopy(maxs, ed->rbe.maxs);
		ed->rbe.modelindex = modelindex;
		ed->rbe.jolt_geomtype = geomtype;
		ed->rbe.jolt_movetype = movetype;
		ed->rbe.jolt_scale = scale;
		VectorAvg(mins, maxs, center);
		ed->rbe.movelimit = std::min(size[0], std::min(size[1], size[2]));
		ed->rbe.mass = mass;

		switch (geomtype)
		{
			case GEOMTYPE_TRIMESH:
			{
				if (!model)
				{
					// Con_Printf("Jolt: entity %i (classname %s) has no model\n", NUM_FOR_EDICT(world->progs, (edict_t *)ed), PR_GetString(world->progs, ed->v->classname));
					World_Jolt_RemoveFromEntity(world, ed);
					return;
				}

				// if we have brushes, make some convex hulls out of them
				if (model->funcs.EnumerateBrushes)
				{
					enumeratebrushes_userdata.world = world;
					enumeratebrushes_userdata.model = model;
					enumeratebrushes_userdata.ed = ed;
					numbrushes = model->funcs.EnumerateBrushes(model, EnumerateBrushes_Callback, (void *)&enumeratebrushes_userdata);
				}
				if (enumeratebrushes_userdata.convex_hull_shape_settings.size() > 0)
				{
					VectorClear(center); // brush vertices are already model-relative
					JPH::ShapeSettings::ShapeResult result;
					if (enumeratebrushes_userdata.static_compound_shape_settings.mSubShapes.size() >= 2)
					{
						geom = new JPH::StaticCompoundShape(enumeratebrushes_userdata.static_compound_shape_settings, ctx->temp_allocator, result);
					}
					else
					{
						geom = new JPH::ConvexHullShape(*enumeratebrushes_userdata.convex_hull_shape_settings[0], result);
					}

					if (result.HasError())
					{
						Con_Printf("Jolt: %s\n", result.GetError().c_str());
						World_Jolt_RemoveFromEntity(world, ed);
						return;
					}
				}
				else if (numbrushes)
				{
					Con_Printf("Jolt: entity %i (classname %s) failed to generate collision mesh for brushes\n", NUM_FOR_EDICT(world->progs, (edict_t *)ed), PR_GetString(world->progs, ed->v->classname));
					World_Jolt_RemoveFromEntity(world, ed);
					return;
				}
				else
				{
					// makes things simpler
					if (NUM_FOR_EDICT(world->progs, ed) == 0 || (model->type == mod_brush && DotProduct(ed->v->origin, ed->v->origin) == 0))
						VectorCopy(ed->v->origin, center);

					if (!rbefuncs->GenerateCollisionMesh(world, model, ed, center))
					{
						Con_Printf("Jolt: entity %i (classname %s) failed to generate collision mesh\n", NUM_FOR_EDICT(world->progs, (edict_t *)ed), PR_GetString(world->progs, ed->v->classname));
						World_Jolt_RemoveFromEntity(world, ed);
						return;
					}

					// we can only make a MeshShape out of worldspawn
					if (NUM_FOR_EDICT(world->progs, ed) == 0)
					{
						// read in vertices
						JPH::VertexList vertices;
						for (int i = 0; i < ed->rbe.numvertices; i++)
							vertices.push_back(JPH::Float3(ed->rbe.vertex3f[i * 3 + 0] * InchesToMeters, ed->rbe.vertex3f[i * 3 + 1] * InchesToMeters, ed->rbe.vertex3f[i * 3 + 2] * InchesToMeters));

						// read in triangles
						JPH::IndexedTriangleList triangles;
						for (int i = 0; i < ed->rbe.numtriangles; i++)
							triangles.push_back(JPH::IndexedTriangle(ed->rbe.element3i[i * 3 + 0], ed->rbe.element3i[i * 3 + 1], ed->rbe.element3i[i * 3 + 2], 0));

						// create mesh
						JPH::ShapeSettings::ShapeResult result;
						mesh_shape_settings.mTriangleVertices = vertices;
						mesh_shape_settings.mIndexedTriangles = triangles;
						mesh_shape_settings.Sanitize();
						geom = new JPH::MeshShape(mesh_shape_settings, result);
						if (result.HasError())
						{
							Con_Printf("Jolt: %s\n", result.GetError().c_str());
							World_Jolt_RemoveFromEntity(world, ed);
							return;
						}
					}
					else
					{
						// otherwise, make a ConvexHullShape

						// read in vertices
						for (int i = 0; i < ed->rbe.numvertices; i++)
							convex_hull_shape_settings.mPoints.push_back(JPH::Vec3(ed->rbe.vertex3f[i * 3 + 0], ed->rbe.vertex3f[i * 3 + 1], ed->rbe.vertex3f[i * 3 + 2]) * InchesToMeters);

						convex_hull_shape_settings.mMaxConvexRadius = 0;

						// create shape
						JPH::ShapeSettings::ShapeResult result;
						geom = new JPH::ConvexHullShape(convex_hull_shape_settings, result);
						if (result.HasError())
						{
							Con_Printf("Jolt: %s\n", result.GetError().c_str());
							World_Jolt_RemoveFromEntity(world, ed);
							return;
						}
					}
				}
				break;
			}

			case GEOMTYPE_BOX:
			{
				geom = new JPH::BoxShape(JPH::Vec3Arg(size[0], size[1], size[2]) * 0.5 * InchesToMeters, 0);
				break;
			}

			case GEOMTYPE_SPHERE:
			{
				geom = new JPH::SphereShape(size[0] * 0.5f * InchesToMeters);
				break;
			}

			case GEOMTYPE_CAPSULE:
			case GEOMTYPE_CAPSULE_X:
			case GEOMTYPE_CAPSULE_Y:
			case GEOMTYPE_CAPSULE_Z:
			{
				int axisindex;
				vec_t radius, length;
				if (geomtype == GEOMTYPE_CAPSULE)
				{
					// the qc gives us 3 axis radius, the longest axis is the capsule axis
					axisindex = 0;
					if (size[axisindex] < size[1])
						axisindex = 1;
					if (size[axisindex] < size[2])
						axisindex = 2;
				}
				else
					axisindex = geomtype - GEOMTYPE_CAPSULE_X;

				if (axisindex == 0)
					radius = std::min(size[1], size[2]) * 0.5f;
				else if (axisindex == 1)
					radius = std::min(size[0], size[2]) * 0.5f;
				else
					radius = std::min(size[0], size[1]) * 0.5f;

				length = size[axisindex] - radius * 2;

				if (length <= 0)
					geom = new JPH::SphereShape(std::min(radius, size[axisindex] * 0.5f) * InchesToMeters);
				else
				{
					geom = new JPH::CapsuleShape(length * 0.5f * InchesToMeters, radius * InchesToMeters);
					geom = new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(),
						JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), (axisindex == 0 ? JPH::Vec3::sAxisX() : axisindex == 1 ? JPH::Vec3::sAxisY() : JPH::Vec3::sAxisZ())), geom);
				}
				break;
			}

			case GEOMTYPE_CYLINDER:
			case GEOMTYPE_CYLINDER_X:
			case GEOMTYPE_CYLINDER_Y:
			case GEOMTYPE_CYLINDER_Z:
			{
				int axisindex;
				vec_t radius, length;
				if (geomtype == GEOMTYPE_CYLINDER)
				{
					// the qc gives us 3 axis radius, the longest axis is the capsule axis
					axisindex = 0;
					if (size[axisindex] < size[1])
						axisindex = 1;
					if (size[axisindex] < size[2])
						axisindex = 2;
				}
				else
					axisindex = geomtype - GEOMTYPE_CYLINDER_X;

				if (axisindex == 0)
					radius = std::min(size[1], size[2]) * 0.5f;
				else if (axisindex == 1)
					radius = std::min(size[0], size[2]) * 0.5f;
				else
					radius = std::min(size[0], size[1]) * 0.5f;

				length = size[axisindex] - radius * 2;

				if (length <= 0)
				{
					radius -= (1 - length) * 0.5;
					length = 1;
				}

				// Jolt takes the half-height of the complete cylinder.
				geom = new JPH::CylinderShape(size[axisindex] * 0.5f * InchesToMeters, radius * InchesToMeters, 0);
				geom = new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(),
					JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), (axisindex == 0 ? JPH::Vec3::sAxisX() : axisindex == 1 ? JPH::Vec3::sAxisY() : JPH::Vec3::sAxisZ())), geom);
				break;
			}

			default:
			{
				Con_Printf("Jolt: entity %i (classname %s) has invalid geomtype\n", NUM_FOR_EDICT(world->progs, (edict_t *)ed), PR_GetString(world->progs, ed->v->classname));
				World_Jolt_RemoveFromEntity(world, ed);
				return;
			}
		}

		// Every stored shape owns one reference, independent of the body's reference.
		// Keep the QC origin as the transform origin, even for asymmetric bounds.
		if (DotProduct(center, center) != 0)
			geom = new JPH::RotatedTranslatedShape(JPH::Vec3(center[0], center[1], center[2]) * InchesToMeters, JPH::Quat::sIdentity(), geom);
		geom->AddRef();
		ed->rbe.body.geom = (void *)geom.GetPtr();
	}


	if (movetype == MOVETYPE_PHYSICS && ed->rbe.mass)
	{
		if (ed->rbe.body.body == nullptr)
		{
			// Con_Printf("created: %s\n", PR_GetString(world->progs, ed->v->classname));
			// setup
			JPH::BodyCreationSettings body_creation_settings;
			body_creation_settings.SetShape((JPH::Shape *)ed->rbe.body.geom);
			body_creation_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			body_creation_settings.mMassPropertiesOverride.mMass = ed->rbe.mass;
			body_creation_settings.mObjectLayer = LAYER_MOVING;
			body_creation_settings.mMotionType = JPH::EMotionType::Dynamic;
			body_creation_settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

			// create body
			body = body_interface.CreateBody(body_creation_settings);
			if (!body)
			{
				Con_Printf("Jolt: body allocation failed\n");
				World_Jolt_RemoveFromEntity(world, ed);
				return;
			}
			body_interface.SetUserData(body->GetID(), NUM_FOR_EDICT(world->progs, ed));

			//motion threshhold should be speed/physicsframerate.
			//Threshhold enables CCD when the object moves faster than X
			//FIXME: recalculate...
			//body->setCcdMotionThreshold((geomsize[0]+geomsize[1]+geomsize[2])*(4/3));
			//radius should be the body's radius, or smaller.
			//body->setCcdSweptSphereRadius((geomsize[0]+geomsize[1]+geomsize[2])*(0.5/3));

			// add to world
			body_interface.AddBody(body->GetID(), body->IsStatic() ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
			modified = qtrue;
			ed->rbe.body.body = (void *)body;
		}
	}
	else
	{
		if (ed->rbe.body.body == nullptr)
		{
			// Con_Printf("created: %s\n", PR_GetString(world->progs, ed->v->classname));
			// setup
			JPH::BodyCreationSettings body_creation_settings;
			body_creation_settings.SetShape((JPH::Shape *)ed->rbe.body.geom);
			body_creation_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
			body_creation_settings.mMassPropertiesOverride.mMass = ed->rbe.mass;
			if (ed->rbe.mass)
			{
				body_creation_settings.mObjectLayer = LAYER_MOVING;
				body_creation_settings.mMotionType = JPH::EMotionType::Dynamic;
			}
			else
			{
				body_creation_settings.mObjectLayer = LAYER_STATIC;
				body_creation_settings.mMotionType = JPH::EMotionType::Static;
			}

			// create body
			body = body_interface.CreateBody(body_creation_settings);
			if (!body)
			{
				Con_Printf("Jolt: body allocation failed\n");
				World_Jolt_RemoveFromEntity(world, ed);
				return;
			}
			body_interface.SetUserData(body->GetID(), NUM_FOR_EDICT(world->progs, ed));

			// add to world
			body_interface.AddBody(body->GetID(), body->IsStatic() ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
			modified = qtrue;
			ed->rbe.body.body = (void *)body;
		}
	}

	body = (JPH::Body *)ed->rbe.body.body;

	// get current data from entity
	VectorCopy(ed->v->origin, origin);
	VectorCopy(ed->v->velocity, velocity);
	VectorCopy(ed->v->angles, angles);
	VectorCopy(ed->v->avelocity, avelocity);

	// compatibility for legacy entities
	{
		vec3_t qangles, qavelocity;
		VectorCopy(angles, qangles);
		VectorCopy(avelocity, qavelocity);

		if (ed->v->modelindex)
		{
			model = world->Get_CModel(world, ed->v->modelindex);
			if (!model || model->type == mod_alias)
			{
				qangles[PITCH] *= pr_meshpitch->value;
				qavelocity[PITCH] *= pr_meshpitch->value;
			}
		}

		rbefuncs->AngleVectors(qangles, forward, left, up);
		VectorNegate(left,left);
		// convert single-axis rotations in avelocity to spinvelocity
		// FIXME: untested math - check signs
		VectorSet(spinvelocity, DEG2RAD(qavelocity[ROLL]), DEG2RAD(qavelocity[PITCH]), DEG2RAD(qavelocity[YAW]));
	}

	// compatibility for legacy entities
	switch (solid)
	{
		case SOLID_BBOX:
		case SOLID_SLIDEBOX:
		case SOLID_CORPSE:
			VectorSet(forward, 1, 0, 0);
			VectorSet(left, 0, 1, 0);
			VectorSet(up, 0, 0, 1);
			VectorSet(spinvelocity, 0, 0, 0);
			break;
	}

	// check if the qc edited any position data
	if (!VectorCompare(angles, ed->rbe.angles) ||
		!VectorCompare(origin, ed->rbe.origin) ||
		!VectorCompare(velocity, ed->rbe.velocity) ||
		!VectorCompare(avelocity, ed->rbe.avelocity) ||
		ed->xv->friction != ed->rbe.friction ||
		ed->xv->gravity != ed->rbe.gravity ||
		ed->xv->damp_linear != ed->rbe.damp_linear ||
		ed->xv->damp_angular != ed->rbe.damp_angular ||
		ed->xv->max_angular != ed->rbe.max_angular ||
		ed->xv->alloweddof != ed->rbe.alloweddof ||
		!VectorCompare(ed->xv->gravitydir, ed->rbe.jolt_gravitydir))
		modified = qtrue;

	// store the qc values into the physics engine
	if (modified && body)
	{
		// values for BodyFromEntity to check if the qc modified anything later
		JPH::Mat44 basis(JPH::Vec4(forward[0], forward[1], forward[2], 0),
			JPH::Vec4(left[0], left[1], left[2], 0), JPH::Vec4(up[0], up[1], up[2], 0), JPH::Vec4(0, 0, 0, 1));
		body_interface.SetPositionAndRotation(body->GetID(), JPH::RVec3(origin[0], origin[1], origin[2]) * InchesToMeters,
			basis.GetQuaternion(), body->IsStatic() ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		VectorCopy(origin, ed->rbe.origin);
		VectorCopy(angles, ed->rbe.angles);
		if (!body->IsStatic())
		{
			body_interface.SetLinearVelocity(body->GetID(), JPH::Vec3(velocity[0], velocity[1], velocity[2]) * InchesToMeters);
			body_interface.SetAngularVelocity(body->GetID(), JPH::Vec3(spinvelocity[0], spinvelocity[1], spinvelocity[2]));
		}
		VectorCopy(velocity, ed->rbe.velocity);
		VectorCopy(avelocity, ed->rbe.avelocity);

		JPH::MotionProperties *motion_properties = body->GetMotionPropertiesUnchecked();

		if (modified || ed->xv->damp_linear != ed->rbe.damp_linear)
		{
			ed->rbe.damp_linear = ed->xv->damp_linear;
			if (motion_properties)
				motion_properties->SetLinearDamping(ed->xv->damp_linear);
		}

		if (modified || ed->xv->damp_angular != ed->rbe.damp_angular)
		{
			ed->rbe.damp_angular = ed->xv->damp_angular;
			if (motion_properties)
				motion_properties->SetAngularDamping(ed->xv->damp_angular);
		}

		if (ed->xv->max_angular != ed->rbe.max_angular ||
		modified || ed->xv->alloweddof != ed->rbe.alloweddof)
		{
			ed->rbe.max_angular = ed->xv->max_angular;
			if (motion_properties)
				motion_properties->SetMaxAngularVelocity(ed->xv->max_angular);
		}

		if (modified || ed->xv->alloweddof != ed->rbe.alloweddof)
		{
			ed->rbe.alloweddof = ed->xv->alloweddof;
			if (motion_properties)
			{
				JPH::EAllowedDOFs alloweddof = JPH::EAllowedDOFs::None;
				JPH::MassProperties mass_properties = ((JPH::Shape *)ed->rbe.body.geom)->GetMassProperties();

				// TODO: translate coordinate space for rotations
				if ((int)ed->xv->alloweddof == ALLOWEDDOF_ALL)
				{
					alloweddof = JPH::EAllowedDOFs::All;
				}
				else
				{
					if ((int)ed->xv->alloweddof & ALLOWEDDOF_MOVE_X)
						alloweddof |= JPH::EAllowedDOFs::TranslationX;
					if ((int)ed->xv->alloweddof & ALLOWEDDOF_MOVE_Y)
						alloweddof |= JPH::EAllowedDOFs::TranslationY;
					if ((int)ed->xv->alloweddof & ALLOWEDDOF_MOVE_Z)
						alloweddof |= JPH::EAllowedDOFs::TranslationZ;
					if ((int)ed->xv->alloweddof & ALLOWEDDOF_ROTATE_X)
						alloweddof |= JPH::EAllowedDOFs::RotationX;
					if ((int)ed->xv->alloweddof & ALLOWEDDOF_ROTATE_Y)
						alloweddof |= JPH::EAllowedDOFs::RotationY;
					if ((int)ed->xv->alloweddof & ALLOWEDDOF_ROTATE_Z)
						alloweddof |= JPH::EAllowedDOFs::RotationZ;
				}

				// dont crash
				if (alloweddof == JPH::EAllowedDOFs::None)
					alloweddof = JPH::EAllowedDOFs::All;

				mass_properties.ScaleToMass(ed->rbe.mass);
				motion_properties->SetMassProperties(alloweddof, mass_properties);
			}
		}

		if (ed->xv->friction != ed->rbe.friction)
		{
			ed->rbe.friction = ed->xv->friction;
			body_interface.SetFriction(body->GetID(), ed->xv->friction);
		}

		if (ed->xv->gravity != ed->rbe.gravity)
		{
			ed->rbe.gravity = ed->xv->gravity;
			if (!body->IsStatic())
				body_interface.SetGravityFactor(body->GetID(), ed->xv->gravity == 0 ? 1 : ed->xv->gravity);
		}

		VectorCopy(ed->xv->gravitydir, ed->rbe.jolt_gravitydir);
		//something changed. make sure it still falls over appropriately
		if (!body->IsStatic())
			body_interface.ActivateBody(body->GetID());
	}
}

static void World_Jolt_BodyToEntity(world_t *world, wedict_t *ed)
{
	model_t *model;
	vec3_t angles;
	vec3_t avelocity;
	vec3_t origin;
	vec3_t velocity;

	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	JPH::BodyInterface &body_interface = ctx->physics_system.GetBodyInterface();

	JPH::Body *body = (JPH::Body *)ed->rbe.body.body;

	if (!body || !ed->rbe.physics || ed->v->movetype != MOVETYPE_PHYSICS)
		return;

	JPH::RVec3 p = body_interface.GetPosition(body->GetID()) * MetersToInches;
	JPH::Quat q = body_interface.GetRotation(body->GetID());
	JPH::Vec3 qa = q.GetEulerAngles();
	JPH::Vec3 v = body_interface.GetLinearVelocity(body->GetID()) * MetersToInches;
	JPH::Vec3 a = body_interface.GetAngularVelocity(body->GetID());

	// copy to vectors
	origin[0] = p[0];
	origin[1] = p[1];
	origin[2] = p[2];
	velocity[0] = v[0];
	velocity[1] = v[1];
	velocity[2] = v[2];
	avelocity[PITCH] = RAD2DEG(a[1]);
	avelocity[YAW] = RAD2DEG(a[2]);
	avelocity[ROLL] = RAD2DEG(a[0]);
	angles[PITCH] = RAD2DEG(qa[1]);
	angles[YAW] = RAD2DEG(qa[2]);
	angles[ROLL] = RAD2DEG(qa[0]);

	if (ed->v->modelindex)
	{
		model = world->Get_CModel(world, ed->v->modelindex);
		if (!model || model->type == mod_alias)
		{
			angles[PITCH] *= pr_meshpitch->value;
			avelocity[PITCH] *= pr_meshpitch->value;
		}
	}

	// copy to entity
	VectorCopy(origin, ed->v->origin);
	VectorCopy(velocity, ed->v->velocity);
	VectorCopy(angles, ed->v->angles);
	VectorCopy(avelocity, ed->v->avelocity);

	// values for BodyFromEntity to check if the qc modified anything later
	VectorCopy(origin, ed->rbe.origin);
	VectorCopy(velocity, ed->rbe.velocity);
	VectorCopy(angles, ed->rbe.angles);
	VectorCopy(avelocity, ed->rbe.avelocity);
	ed->rbe.gravity = body_interface.GetGravityFactor(body->GetID());
	ed->rbe.friction = body_interface.GetFriction(body->GetID());

	JPH::MotionProperties *motion_properties = body->GetMotionPropertiesUnchecked();

	if (motion_properties)
	{
		ed->rbe.damp_linear = motion_properties->GetLinearDamping();
		ed->rbe.damp_angular = motion_properties->GetAngularDamping();
		ed->rbe.max_angular = motion_properties->GetMaxAngularVelocity();
	}

	rbefuncs->LinkEdict(world, ed, qtrue);

	// Con_Printf("updated: %s\n", PR_GetString(world->progs, ed->v->classname));
}

static void World_Jolt_JointFromEntity(world_t *world, wedict_t *ed)
{
	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	JPH::Constraint *joint = nullptr;
	int movetype = ed->v->movetype;
	JPH::Body *enemybody, *aimentbody;
	wedict_t *enemy, *aiment;
	vec_t cfm, erp, fmax;
	vec_t stop;
	vec_t vel;
	vec3_t forward, left, up;
	JPH::Vec3 origin(ed->v->origin[0], ed->v->origin[1], ed->v->origin[2]);

	// nothing to do
	if (ed->xv->jointtype == 0)
	{
		World_Jolt_RemoveJointFromEntity(world, ed);
		return;
	}

	// can't have both
	if (movetype == MOVETYPE_PHYSICS)
		return;

	enemy = (wedict_t *)PROG_TO_EDICT(world->progs, ed->v->enemy);
	enemybody = (JPH::Body *)enemy->rbe.body.body;
	if (ED_ISFREE(enemy) || !enemybody)
	{
		enemy = (wedict_t *)PROG_TO_EDICT(world->progs, 0);
		enemybody = (JPH::Body *)enemy->rbe.body.body;
	}

	aiment = (wedict_t *)PROG_TO_EDICT(world->progs, ed->v->aiment);
	aimentbody = (JPH::Body *)aiment->rbe.body.body;
	if (ED_ISFREE(aiment) || !aimentbody)
	{
		aiment = (wedict_t *)PROG_TO_EDICT(world->progs, 0);
		aimentbody = (JPH::Body *)aiment->rbe.body.body;
	}

	// FIXME: ??
	if (!enemybody || !aimentbody || enemy == aiment)
		return;

	// FIXME: fucking darkplaces
	if (ed->v->movedir[0] > 0 && ed->v->movedir[1] > 0)
	{
		float step = 1.0f / physics_jolt_framerate->value;
		float k = ed->v->movedir[0];
		float d = ed->v->movedir[1];
		float r = 2.0 * d * sqrt(k); // we assume d is premultiplied by sqrt(springMass)
		cfm = 1.0 / (step * k + r); // always > 0
		erp = step * k * cfm;
		vel = 0;
		fmax = 0;
		stop = ed->v->movedir[2] * InchesToMeters;
	}
	else if (ed->v->movedir[1] < 0)
	{
		cfm = 0;
		erp = 0;
		vel = ed->v->movedir[0];
		fmax = -ed->v->movedir[1];
		stop = ed->v->movedir[2] > 0 ? ed->v->movedir[2] * InchesToMeters : INFINITY;
	}
	else
	{
		cfm = 0;
		erp = 0;
		vel = 0;
		fmax = 0;
		stop = INFINITY;
	}

	if (ed->xv->jointtype == ed->rbe.joint_type &&
		VectorCompare(ed->v->origin, ed->rbe.joint_origin) &&
		VectorCompare(ed->v->velocity, ed->rbe.joint_velocity) &&
		VectorCompare(ed->v->angles, ed->rbe.joint_angles) &&
		ed->v->enemy == ed->rbe.joint_enemy &&
		ed->v->aiment == ed->rbe.joint_aiment &&
		VectorCompare(ed->v->movedir, ed->rbe.joint_movedir))
		return; // nothing to do

	if (ed->rbe.joint.joint)
	{
		joint = (JPH::Constraint *)ed->rbe.joint.joint;
		ctx->physics_system.RemoveConstraint(joint);
		ed->rbe.joint.joint = nullptr;
		joint = nullptr;
	}

	JPH::Vec3 enemyorigin(enemy->v->origin[0], enemy->v->origin[1], enemy->v->origin[2]);
	JPH::Vec3 aimentorigin(aiment->v->origin[0], aiment->v->origin[1], aiment->v->origin[2]);

	ed->rbe.joint_type = ed->xv->jointtype;
	ed->rbe.joint_enemy = ed->v->enemy;
	ed->rbe.joint_aiment = ed->v->aiment;
	VectorCopy(ed->v->origin, ed->rbe.joint_origin);
	VectorCopy(ed->v->velocity, ed->rbe.joint_velocity);
	VectorCopy(ed->v->angles, ed->rbe.joint_angles);
	VectorCopy(ed->v->movedir, ed->rbe.joint_movedir);

	rbefuncs->AngleVectors(ed->v->angles, forward, left, up);
	VectorNegate(left, left);

	switch ((int)ed->xv->jointtype)
	{
		case JOINTTYPE_FIXED:
		{
			JPH::FixedConstraintSettings settings;
			settings.mPoint1 = origin * InchesToMeters;
			settings.mPoint2 = origin * InchesToMeters;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;

			joint = new JPH::FixedConstraint(*enemybody, *aimentbody, settings);
			break;
		}

		case JOINTTYPE_POINT:
		{
			JPH::PointConstraintSettings settings;
			settings.mPoint1 = origin * InchesToMeters;
			settings.mPoint2 = origin * InchesToMeters;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;

			joint = new JPH::PointConstraint(*enemybody, *aimentbody, settings);
			break;
		}

		case JOINTTYPE_HINGE:
		{
			JPH::HingeConstraintSettings settings;
			settings.mPoint1 = origin * InchesToMeters;
			settings.mPoint2 = origin * InchesToMeters;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mHingeAxis1 = settings.mHingeAxis2 = JPH::Vec3(forward[0], forward[1], forward[2]);
			settings.mNormalAxis1 = settings.mNormalAxis2 = settings.mHingeAxis1.GetNormalizedPerpendicular();

			joint = new JPH::HingeConstraint(*enemybody, *aimentbody, settings);
			break;
		}

		case JOINTTYPE_SLIDER:
		{
			JPH::SliderConstraintSettings settings;
			settings.mPoint1 = origin * InchesToMeters;
			settings.mPoint2 = origin * InchesToMeters;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mSliderAxis1 = settings.mSliderAxis2 = JPH::Vec3(forward[0], forward[1], forward[2]);
			settings.mNormalAxis1 = settings.mNormalAxis2 = settings.mSliderAxis1.GetNormalizedPerpendicular();
			settings.mLimitsMin = -stop;
			settings.mLimitsMax = stop;

			// FIXME: is this stupid?
			settings.mLimitsSpringSettings.mFrequency = ed->xv->friction;

			joint = new JPH::SliderConstraint(*enemybody, *aimentbody, settings);
			break;
		}
	}

	ed->rbe.joint.joint = (void *)joint;
	if (joint)
	{
		joint->SetUserData(NUM_FOR_EDICT(world->progs, ed));
		ctx->physics_system.AddConstraint(joint);
	}
}

static void QDECL World_Jolt_RunFrame(world_t *world, double frametime, double gravity)
{
	joltcontext_t *ctx = (joltcontext_t *)world->rbe;

	// no physics objects active
	if (!world->rbe_hasphysicsents)
		return;

	// fix up cvars
	if (!std::isfinite(physics_jolt_framerate->value) || physics_jolt_framerate->value <= 0)
		cvarfuncs->SetFloat("physics_jolt_framerate", 60);
	if (!std::isfinite(physics_jolt_maxiterationsperframe->value) || physics_jolt_maxiterationsperframe->value <= 0)
		cvarfuncs->SetFloat("physics_jolt_maxiterationsperframe", 1);

	// copy physics properties from entities to physics engine
	for (unsigned int i = 0; i < world->num_edicts; i++)
	{
		wedict_t *ed = WEDICT_NUM_PB(world->progs, i);
		if (!ED_ISFREE(ed))
			World_Jolt_BodyFromEntity(world, ed);
	}

	// oh, and it must be called after all bodies were created
	for (unsigned int i = 0; i < world->num_edicts; i++)
	{
		wedict_t *ed = WEDICT_NUM_PB(world->progs, i);
		if (!ED_ISFREE(ed))
			World_Jolt_JointFromEntity(world, ed);
	}

	// process command queue
	while (ctx->cmdqueuehead)
	{
		rbecommandqueue_t *cmd = ctx->cmdqueuehead;
		ctx->cmdqueuehead = cmd->next;
		if (!cmd->next)
			ctx->cmdqueuetail = nullptr;
		World_Jolt_RunCommand(world, cmd);
		BZ_Free(cmd);
	}

	// set gravity
	ctx->physics_system.SetGravity(JPH::Vec3::sZero());

	// optimize broadphase only once
	if (!ctx->broadphase_optimized)
	{
		ctx->physics_system.OptimizeBroadPhase();
		ctx->broadphase_optimized = qtrue;
	}

	// step the world
	const double step = 1.0 / std::max(1.0, std::min(1000.0, (double)physics_jolt_framerate->value));
	const int maxsteps = (int)std::max(1.0, std::min(64.0, (double)physics_jolt_maxiterationsperframe->value));
	if (std::isfinite(frametime) && frametime > 0)
		ctx->accumulator = std::min(ctx->accumulator + frametime, step * maxsteps);
	for (int i = 0; i < maxsteps && ctx->accumulator + 1e-9 >= step; ++i)
	{
		// Per-entity gravity uses the same QC fields as native FTE movement.
		for (unsigned int n = 1; n < world->num_edicts; ++n)
		{
			wedict_t *ed = WEDICT_NUM_PB(world->progs, n);
			auto *body = (JPH::Body *)ed->rbe.body.body;
			if (ED_ISFREE(ed) || !body || !body->IsDynamic() || !body->IsActive())
				continue;
			JPH::Vec3 direction(ed->xv->gravitydir[0], ed->xv->gravitydir[1], ed->xv->gravitydir[2]);
			direction = direction.NormalizedOr(-JPH::Vec3::sAxisZ());
			float strength = gravity * (ed->xv->gravity == 0 ? 1 : ed->xv->gravity) * InchesToMeters;
			ctx->physics_system.GetBodyInterface().AddForce(body->GetID(), direction * (strength / body->GetMotionProperties()->GetInverseMass()));
		}
		ctx->physics_system.Update((float)step, 1, &ctx->temp_allocator, &ctx->job_system);
		ctx->accumulator -= step;
	}

	// copy physics properties from physics engine to entities
	for (unsigned int i = 0; i < world->num_edicts; i++)
	{
		wedict_t *ed = WEDICT_NUM_PB(world->progs, i);
		if (!ED_ISFREE(ed))
			World_Jolt_BodyToEntity(world, ed);
	}
	if (world->Event_Touch)
	{
		std::sort(ctx->contacts.begin(), ctx->contacts.end(), [](const joltcontext::ContactEvent &a, const joltcontext::ContactEvent &b) {
			return a.a.GetIndexAndSequenceNumber() != b.a.GetIndexAndSequenceNumber() ? a.a.GetIndexAndSequenceNumber() < b.a.GetIndexAndSequenceNumber() : a.b.GetIndexAndSequenceNumber() < b.b.GetIndexAndSequenceNumber();
		});
		JPH::BodyID last_a, last_b;
		for (const auto &contact : ctx->contacts)
		{
			if (contact.a == last_a && contact.b == last_b)
				continue;
			last_a = contact.a; last_b = contact.b;
			auto &bodies = ctx->physics_system.GetBodyInterface();
			JPH::BodyLockRead lock_a(ctx->physics_system.GetBodyLockInterface(), contact.a);
			if (!lock_a.Succeeded()) continue;
			unsigned int na = (unsigned int)lock_a.GetBody().GetUserData();
			lock_a.ReleaseLock();
			JPH::BodyLockRead lock_b(ctx->physics_system.GetBodyLockInterface(), contact.b);
			if (!lock_b.Succeeded()) continue;
			unsigned int nb = (unsigned int)lock_b.GetBody().GetUserData();
			lock_b.ReleaseLock();
			if (na >= world->num_edicts || nb >= world->num_edicts) continue;
			wedict_t *a = WEDICT_NUM_PB(world->progs, na), *b = WEDICT_NUM_PB(world->progs, nb);
			trace_t trace = {};
			for (int axis = 0; axis < 3; ++axis)
			{
				trace.endpos[axis] = contact.point[axis] * MetersToInches;
				trace.plane.normal[axis] = -contact.normal[axis];
			}
			trace.ent = b;
			if (a->v->touch)
				world->Event_Touch(world, a, b, &trace);
			// QC may remove or replace bodies in either callback.
			if (!ED_ISFREE(a) && !ED_ISFREE(b) && a->rbe.body.body && b->rbe.body.body &&
				((JPH::Body *)a->rbe.body.body)->GetID() == contact.a && ((JPH::Body *)b->rbe.body.body)->GetID() == contact.b)
			{
				trace.ent = a;
				VectorNegate(trace.plane.normal, trace.plane.normal);
				if (b->v->touch)
					world->Event_Touch(world, b, a, &trace);
			}
		}
	}
	ctx->contacts.clear();

}

static void QDECL World_Jolt_PushCommand(world_t *world, rbecommandqueue_t *val)
{
	joltcontext_t *ctx = (joltcontext_t *)world->rbe;
	rbecommandqueue_t *cmd = (rbecommandqueue_t *)BZ_Malloc(sizeof(*cmd));
	world->rbe_hasphysicsents = qtrue;
	memcpy(cmd, val, sizeof(*cmd));
	cmd->next = nullptr;
	if (ctx->cmdqueuehead)
	{
		rbecommandqueue_t *oldtail = ctx->cmdqueuetail;
		oldtail->next = ctx->cmdqueuetail = cmd;
	}
	else
	{
		ctx->cmdqueuetail = ctx->cmdqueuehead = cmd;
	}
}

// Trace against simulated bodies; FTE keeps responsibility for ordinary BSP/world
// traces. This callback receives the actual requested bounds, including traceline.
static void QDECL World_Jolt_TraceMove(world_t *world, wedict_t *ed, vec3_t start,
	vec3_t mins, vec3_t maxs, vec3_t end, int type, unsigned int contentsmask, trace_t *trace)
{
	auto *ctx = (joltcontext_t *)world->rbe;
	auto &query = ctx->physics_system.GetNarrowPhaseQuery();
	auto &bodies = ctx->physics_system.GetBodyInterface();
	if (type & MOVE_NOMONSTERS || !(contentsmask & (FTECONTENTS_SOLID | FTECONTENTS_BODY)))
		return;

	class Filter : public JPH::BodyFilter
	{
	public:
		world_t *world;
		wedict_t *self;
		JPH::BodyInterface *bodies;
		bool ShouldCollide(const JPH::BodyID &id) const override
		{
			unsigned int num = (unsigned int)bodies->GetUserData(id);
			if (num >= world->num_edicts)
				return false;
			wedict_t *other = WEDICT_NUM_PB(world->progs, num);
			if (ED_ISFREE(other) || other == self || other->v->movetype != MOVETYPE_PHYSICS || other->v->solid == SOLID_NOT || other->v->solid == SOLID_TRIGGER)
				return false;
			if (self)
			{
				if (!((int)self->xv->dimension_hit & (int)other->xv->dimension_solid))
					return false;
				if (PROG_TO_WEDICT(world->progs, other->v->owner) == self || PROG_TO_WEDICT(world->progs, self->v->owner) == other)
					return false;
			}
			return true;
		}
	} filter;
	filter.world = world;
	filter.self = ed;
	filter.bodies = &bodies;
	JPH::Vec3 delta = JPH::Vec3(end[0]-start[0], end[1]-start[1], end[2]-start[2]) * InchesToMeters;
	JPH::RVec3 origin = JPH::RVec3(start[0], start[1], start[2]) * InchesToMeters;
	JPH::RefConst<JPH::Shape> shape;
	bool point = VectorCompare(mins, maxs);
	if (point)
		shape = new JPH::SphereShape(0.000001f);
	else
		shape = new JPH::BoxShape(JPH::Vec3(std::max(0.000002f, (maxs[0]-mins[0])*InchesToMeters),
			std::max(0.000002f, (maxs[1]-mins[1])*InchesToMeters), std::max(0.000002f, (maxs[2]-mins[2])*InchesToMeters)) * 0.5f, 0);
	origin += JPH::Vec3((mins[0]+maxs[0])*0.5f, (mins[1]+maxs[1])*0.5f, (mins[2]+maxs[2])*0.5f)*InchesToMeters;
	JPH::RMat44 transform = JPH::RMat44::sTranslation(origin);
	JPH::CollideShapeSettings overlapsettings;
	overlapsettings.mMaxSeparationDistance = 0;
	JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> overlap;
	query.CollideShape(shape, JPH::Vec3::sReplicate(1), transform, overlapsettings, origin, overlap, {}, {}, filter);
	bool startsolid = false;
	for (const auto &hit : overlap.mHits)
		if (hit.mPenetrationDepth > 0.00001f || point)
			startsolid = true;
	if (startsolid)
	{
		trace->startsolid = qtrue;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> endoverlap;
		query.CollideShape(shape, JPH::Vec3::sReplicate(1), transform.PostTranslated(delta), overlapsettings, origin + delta, endoverlap, {}, {}, filter);
		for (const auto &hit : endoverlap.mHits)
			if ((hit.mPenetrationDepth > 0.00001f || point) && std::any_of(overlap.mHits.begin(), overlap.mHits.end(),
				[&](const JPH::CollideShapeResult &start) { return start.mBodyID2 == hit.mBodyID2 && start.mSubShapeID2 == hit.mSubShapeID2; }))
			{
				trace->allsolid = qtrue;
				trace->fraction = trace->truefraction = 0;
				VectorCopy(start, trace->endpos);
				trace->ent = WEDICT_NUM_PB(world->progs, bodies.GetUserData(hit.mBodyID2));
				return;
			}
		// Quake permits escaping from a solid start. Keep the native world trace.
		return;
	}
	if (delta.LengthSq() == 0)
		return;
	JPH::AllHitCollisionCollector<JPH::CastShapeCollector> hits;
	JPH::ShapeCastSettings settings;
	settings.mReturnDeepestPoint = true;
	query.CastShape(JPH::RShapeCast(shape, JPH::Vec3::sReplicate(1), transform, delta), settings, origin, hits, {}, {}, filter);
	hits.Sort();
	for (const auto &hit : hits.mHits)
	{
		if (hit.mFraction >= trace->fraction)
			break;
		JPH::Vec3 normal = -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisZ());
		if (normal.Dot(delta) >= 0)
			continue; // tangent or moving away from an initial contact
		trace->fraction = trace->truefraction = std::max(0.0f, hit.mFraction);
		VectorInterpolate(start, trace->fraction, end, trace->endpos);
		trace->plane.normal[0] = normal[0];
		trace->plane.normal[1] = normal[1];
		trace->plane.normal[2] = normal[2];
		trace->plane.dist = DotProduct(trace->plane.normal, trace->endpos);
		trace->ent = WEDICT_NUM_PB(world->progs, bodies.GetUserData(hit.mBodyID2));
		trace->contents = FTECONTENTS_BODY;
		break;
	}
}

static qboolean QDECL World_Jolt_GetBounds(world_t *world, wedict_t *ed, vec3_t mins, vec3_t maxs)
{
	auto *body = (JPH::Body *)ed->rbe.body.body;
	if (!body)
		return qfalse;
	const auto &bounds = body->GetWorldSpaceBounds();
	for (int i = 0; i < 3; ++i)
	{
		mins[i] = bounds.mMin[i] * MetersToInches;
		maxs[i] = bounds.mMax[i] * MetersToInches;
	}
	return qtrue;
}

static void QDECL World_Jolt_Start(world_t *world)
{
	joltcontext_t *ctx;

	// already has one somehow
	if (world->rbe)
		return;

	ctx = new joltcontext_t();
	ctx->funcs.End = World_Jolt_End;
	ctx->funcs.RemoveJointFromEntity = World_Jolt_RemoveJointFromEntity;
	ctx->funcs.RemoveFromEntity = World_Jolt_RemoveFromEntity;
#if 0
	ctx->funcs.RagMatrixToBody = World_Jolt_RagMatrixToBody;
	ctx->funcs.RagCreateBody = World_Jolt_RagCreateBody;
	ctx->funcs.RagMatrixFromJoint = World_Jolt_RagMatrixFromJoint;
	ctx->funcs.RagMatrixFromBody = World_Jolt_RagMatrixFromBody;
	ctx->funcs.RagEnableJoint = World_Jolt_RagEnableJoint;
	ctx->funcs.RagCreateJoint = World_Jolt_RagCreateJoint;
	ctx->funcs.RagDestroyBody = World_Jolt_RagDestroyBody;
	ctx->funcs.RagDestroyJoint = World_Jolt_RagDestroyJoint;
#endif
	ctx->funcs.RunFrame = World_Jolt_RunFrame;
	ctx->funcs.PushCommand = World_Jolt_PushCommand;
	ctx->funcs.TraceMove = World_Jolt_TraceMove;
	ctx->funcs.GetBounds = World_Jolt_GetBounds;
	world->rbe = (rigidbodyengine_t *)ctx;

	ctx->contact_filter.world = world;
	ctx->contact_filter.ctx = ctx;
	ctx->physics_system.SetContactListener(&ctx->contact_filter);

	// init jobs system
	// TODO: use FTE threads
	ctx->job_system.Init(ctx->max_physics_jobs, ctx->max_physics_barriers, 2);

	// init physics system
	ctx->physics_system.Init(
		ctx->max_bodies,
		ctx->num_body_mutexes,
		ctx->max_body_pairs,
		ctx->max_contact_constraints,
		ctx->broad_phase_layer_interface,
		ctx->object_vs_broadphase_layer_filter,
		ctx->object_vs_object_layer_filter
	);

	JPH::PhysicsSettings settings = ctx->physics_system.GetPhysicsSettings();
	settings.mPenetrationSlop = 0.02f * InchesToMeters;
	settings.mMaxPenetrationDistance = 2.0f * InchesToMeters;
	settings.mSpeculativeContactDistance = 0.1f * InchesToMeters;
	ctx->physics_system.SetPhysicsSettings(settings);
	ctx->broadphase_optimized = qfalse;
}

static void QDECL Plug_Jolt_Shutdown(void)
{
	if (rbefuncs)
		rbefuncs->UnregisterPhysicsEngine("Jolt");

	// do this globally rather than per-context
	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;
}

#ifdef JPH_ENABLE_ASSERTS
static bool Plug_Jolt_AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, unsigned int inLine)
{
	char buffer[1024];
	Q_snprintfz(buffer, sizeof(buffer), "%s:%d: (%s) %s", inFile, inLine, inExpression, inMessage ? inMessage : "");
	Con_Printf("Jolt: %.1024s\n", buffer);
	return false;
}
#endif // JPH_ENABLE_ASSERTS

static void Plug_Jolt_TraceImpl(const char *fmt, ...)
{
	char buffer[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);

	Con_Printf("Jolt: %.1024s\n", buffer);
}

extern "C" qboolean Plug_Init(void)
{
	// get rbe funcs
	rbefuncs = (rbeplugfuncs_t *)plugfuncs->GetEngineInterface("RBE", sizeof(*rbefuncs));
	if (!rbefuncs || rbefuncs->version != RBEPLUGFUNCS_VERSION || rbefuncs->wedictsize != sizeof(wedict_t) || !rbefuncs->RegisterPhysicsEngine)
	{
		Con_Printf("Jolt: engine not compatible with physics plugins\n");
		return qfalse;
	}

	// get thread funcs
	threadfuncs = (plugthreadfuncs_t *)plugfuncs->GetEngineInterface(plugthreadfuncs_name, sizeof(*threadfuncs));
	if (!threadfuncs)
	{
		Con_Printf("Jolt: couldn't get multithreading functions\n");
		//return qfalse;
	}

	// register the shutdown func
	if (!plugfuncs->ExportFunction("Shutdown", (funcptr_t)Plug_Jolt_Shutdown))
	{
		Con_Printf("Jolt: plugfuncs->ExportFunction failed\n");
		return qfalse;
	}

	// register the engine
	if (!rbefuncs->RegisterPhysicsEngine("Jolt", World_Jolt_Start))
	{
		Con_Printf("Jolt: engine already has a physics plugin active.\n");
		return qfalse;
	}

	// get cvars
	physics_jolt_maxiterationsperframe = cvarfuncs->GetNVFDG("physics_jolt_maxiterationsperframe", "8", 0, "Maximum times to run the simulation per frame", "Jolt");
	physics_jolt_framerate = cvarfuncs->GetNVFDG("physics_jolt_framerate", "60", 0, "Physics framerate", "Jolt");
	pr_meshpitch = cvarfuncs->GetNVFDG("r_meshpitch", "-1", 0, "", "Jolt");

	// do this globally rather than per-context
	JPH::RegisterDefaultAllocator();
	JPH::Trace = Plug_Jolt_TraceImpl;
	JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = Plug_Jolt_AssertFailedImpl;)
	JPH::Factory::sInstance = new JPH::Factory;
	JPH::RegisterTypes();
	return qtrue;
}
