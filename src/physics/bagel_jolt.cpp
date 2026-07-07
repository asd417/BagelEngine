#include "physics/bagel_jolt.hpp"
#include "bagel_ecs_components.hpp"
#include "../bagel_model.hpp"
#include "../bagel_util.hpp"

#include <glm/gtc/quaternion.hpp>  // mat3_cast / quat_cast / mat4_cast

#include <Jolt/Physics/Collision/RayCast.h>          // RRayCast
#include <Jolt/Physics/Collision/CastResult.h>       // RayCastResult
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h> // physicsSystem.GetNarrowPhaseQuery().CastRay

#include "bagel_imgui.hpp"
#define CONSOLE ConsoleApp::Instance()

namespace bagel {
	BGLJolt* BGLJolt::instance = nullptr;

	// High bit of a body's user data flags "this is a fused group body — resolve a raycast to the
	// specific block via the compound's sub-shape user data." Ordinary bodies store their entity id
	// (a 32-bit value, so this bit is clear) directly in the low bits.
	static constexpr JPH::uint64 kGroupBodyFlag = JPH::uint64(1) << 63;

	// Extract Euler angles in the X*Y*Z order that TransformComponent::computeMat4() reassembles,
	// so the stored eulers reproduce R exactly. (glm::eulerAngles uses a different order, which is
	// why round-tripping physics orientations through it looked wrong.) R is column-major: R[col][row].
	static glm::vec3 eulerXYZFromRot(const glm::mat3& R)
	{
		float ey = glm::asin(glm::clamp(R[2][0], -1.0f, 1.0f));   // math[0][2] = sin(y)
		float ex, ez;
		if (glm::abs(R[2][0]) < 0.99999f) {
			ex = glm::atan(-R[2][1], R[2][2]);   // math[1][2], math[2][2]
			ez = glm::atan(-R[1][0], R[0][0]);   // math[0][1], math[0][0]
		} else {
			ex = glm::atan(R[0][1], R[1][1]);    // gimbal lock (y ≈ ±90°): fold x+z, pin z = 0
			ez = 0.0f;
		}
		return { ex, ey, ez };
	}

	BGLJolt::BGLJolt(BGLDevice& _bglDevice, entt::registry& r ) : registry{ r }, bglDevice{ _bglDevice } {
		physicsSystem.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);
		physicsSystem.SetBodyActivationListener(&body_activation_listener);
		physicsSystem.SetContactListener(&contact_listener);
	
		// The main way to interact with the bodies in the physics system is through the body interface. There is a locking and a non-locking
		// variant of this. We're going to use the locking version (even though we're not planning to access bodies from multiple threads)
		bodyInterface = &physicsSystem.GetBodyInterface();

		// Optional step: Before starting the physics simulation you can optimize the broad phase. This improves collision detection performance (it's pointless here because we only have 2 bodies).
		// You should definitely not call this every frame or when e.g. streaming in a new level section as it is an expensive operation.
		// Instead insert all new objects in batches instead of 1 at a time to keep the broad phase efficient.
		physicsSystem.OptimizeBroadPhase();
	}

	BGLJolt::~BGLJolt()
	{
		// Unregisters all types with the factory and cleans up the default material
		JPH::UnregisterTypes();

		// Destroy the factory
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}

	void BGLJolt::Step(float dt, JPH::uint stepCount)
	{
		for(JPH::uint i = 0;i<stepCount;i++)
		{
			// If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
			const int cCollisionSteps = 1;
			// Step the world
			physicsSystem.Update(dt * simTimeScale, cCollisionSteps, &tempAllocator, &job_system);
		}
	}

	void BGLJolt::SetComponentActivityAll(bool activity)
	{
		auto viewPhysics = registry.view<JoltPhysicsComponent>();
		for (auto [ent,comp] : viewPhysics.each()) {
			if (activity) bodyInterface->ActivateBody(comp.bodyID);
			else bodyInterface->DeactivateBody(comp.bodyID);
		}
		auto viewKinematic = registry.view<JoltKinematicComponent>();
		for (auto [ent, comp] : viewKinematic.each()) {
			if (activity) bodyInterface->ActivateBody(comp.bodyID);
			else bodyInterface->DeactivateBody(comp.bodyID);
		}
	}

	//Assumes that an entity either has JoltPhysicsComponent or JoltKinematicComponent
	void BGLJolt::SetComponentActivity(entt::entity ent, bool activity)
	{
		auto comp = registry.try_get<JoltPhysicsComponent>(ent);
		if (comp == nullptr) auto comp = registry.try_get<JoltKinematicComponent>(ent);
		if (comp == nullptr) return;
		if(activity) bodyInterface->ActivateBody(comp->bodyID);
		else bodyInterface->DeactivateBody(comp->bodyID);
	}

	bool BGLJolt::IsBodyActive(entt::entity ent)
	{
		auto* comp = registry.try_get<JoltPhysicsComponent>(ent);
		if (comp == nullptr || comp->bodyID.IsInvalid()) return false;
		return bodyInterface->IsActive(comp->bodyID);
	}

	void BGLJolt::RemoveEntityBody(entt::entity ent)
	{
		auto destroy = [&](JPH::BodyID id) {
			if (id.IsInvalid()) return;
			bodyInterface->RemoveBody(id);
			bodyInterface->DestroyBody(id);
		};
		if (auto* pc = registry.try_get<JoltPhysicsComponent>(ent))   destroy(pc->bodyID);
		if (auto* kc = registry.try_get<JoltKinematicComponent>(ent)) destroy(kc->bodyID);
	}

	void BGLJolt::ApplyTransformToKinematic(float dt)
	{
		auto entView = registry.view<TransformComponent, JoltKinematicComponent>();
		for (auto [entity, transComp, physComp] : entView.each()) {
			glm::vec3 targetPos = transComp.getWorldTranslation();
			glm::vec3 targetRotation = transComp.getWorldRotation();
			JPH::Vec3 jphPos = { targetPos.x,targetPos.y,targetPos.z };
			JPH::Quat jphRot = JPH::Quat::sEulerAngles({ targetRotation.x,targetRotation.y,targetRotation.z });
			
			switch (physComp.moveMode) {
			case JoltKinematicComponent::MoveMode::PHYSICAL:
				bodyInterface->MoveKinematic(physComp.bodyID, jphPos, jphRot, dt);
				break;
			case JoltKinematicComponent::MoveMode::IMMEDIATE:
				bodyInterface->SetPositionAndRotation(physComp.bodyID, jphPos, jphRot, JPH::EActivation::Activate);
				break;
			case JoltKinematicComponent::MoveMode::IMMEDIATE_OPTIMAL:
				bodyInterface->SetPositionAndRotationWhenChanged(physComp.bodyID, jphPos, jphRot, JPH::EActivation::Activate);
				break;
			}
		}
	}

	void BGLJolt::ApplyPhysicsTransform()
	{
		// Kill floor: anything that falls past this Y is out of the world; queue it for deletion
		// (can't destroy mid-iteration — that invalidates the view).
		constexpr float kKillY = -1000.0f;
		std::vector<entt::entity> fell;

		auto entView = registry.view<TransformComponent, JoltPhysicsComponent>();
		for (auto [ entity, transComp, physComp ] : entView.each()) {

			JPH::RVec3 newPos  = bodyInterface->GetPosition(physComp.bodyID);
			if (newPos.GetY() < kKillY) { fell.push_back(entity); continue; }
			JPH::Quat  newQuat = bodyInterface->GetRotation(physComp.bodyID);
			glm::vec3 newVec = { newPos.GetX(), newPos.GetY(), newPos.GetZ() };

			// NOTE: glm::qua's ctor is (w, x, y, z) — w first. Extract eulers in computeMat4()'s
			// X*Y*Z order (see eulerXYZFromRot) so render matches physics.
			glm::quat q{ newQuat.GetW(), newQuat.GetX(), newQuat.GetY(), newQuat.GetZ() };
			transComp.setTranslation(newVec);
			transComp.setRotation(eulerXYZFromRot(glm::mat3_cast(q)));
		}

		// Destroy the fallen entities' Jolt bodies (the components have no destructor that does
		// this) and then the entities themselves.
		for (entt::entity e : fell) {
			if (auto* pc = registry.try_get<JoltPhysicsComponent>(e); pc && !pc->bodyID.IsInvalid()) {
				bodyInterface->RemoveBody(pc->bodyID);
				bodyInterface->DestroyBody(pc->bodyID);
			}
			registry.destroy(e);
		}
	}

	// Tear down whatever live body/group-wiring a set of parts currently has, so the group can be
	// (re)built cleanly. RemoveEntityBody frees the ref's group body (or a part's own body); the
	// components are then dropped. ColliderShapeComponent is intentionally left intact.
	static void teardownGroupBodies(entt::registry& reg, BGLJolt& jolt, const std::vector<entt::entity>& group)
	{
		for (entt::entity m : group) {
			jolt.RemoveEntityBody(m);
			reg.remove<JoltPhysicsComponent>(m);
			reg.remove<JoltGroupMemberComponent>(m);
		}
	}

	JPH::BodyID BGLJolt::buildGroupCompound(const std::vector<entt::entity>& group, PhysicsType type, JPH::ObjectLayer layer)
	{
		if (group.empty()) return {};
		// Reference frame = the first member's world pose. Everything is derived from computeMat4()
		// (the render transform), so physics ends up aligned with the mesh.
		entt::entity ref = group.front();
		auto* refTc = registry.try_get<TransformComponent>(ref);
		if (!refTc) return {};
		const glm::mat4 refWorld = refTc->computeMat4();
		const glm::mat4 invRef   = glm::inverse(refWorld);

		// Combine each member's baked shape into a compound at its pose relative to the reference,
		// tagging the sub-shape with the member entity so a raycast can resolve back to the block.
		JPH::StaticCompoundShapeSettings comp;
		std::vector<std::pair<entt::entity, glm::mat4>> members;
		for (entt::entity m : group) {
			auto* tc = registry.try_get<TransformComponent>(m);
			auto* sc = registry.try_get<ColliderShapeComponent>(m);
			if (!tc || !sc || sc->shape == nullptr) continue;
			const glm::mat4 rel = invRef * tc->computeMat4();   // member relative to ref
			const glm::vec3 rp  = glm::vec3(rel[3]);
			const glm::quat rq  = glm::quat_cast(glm::mat3(rel));
			comp.AddShape(JPH::Vec3(rp.x, rp.y, rp.z),
			              JPH::Quat(rq.x, rq.y, rq.z, rq.w),
			              sc->shape.GetPtr(), entt::to_integral(m)); // sub-shape user data = block entity
			members.emplace_back(m, rel);
		}
		if (members.size() < 2) return {};   // not enough valid members to fuse

		JPH::ShapeSettings::ShapeResult res = comp.Create();
		if (res.HasError()) {
			CONSOLE->Log("Physics", std::string("group compound failed: ") + res.GetError().c_str());
			return {};
		}

		const JPH::EActivation act = (type == PhysicsType::STATIC) ? JPH::EActivation::DontActivate
		                                                          : JPH::EActivation::Activate;
		const glm::vec3 bp = glm::vec3(refWorld[3]);
		const glm::quat bq = glm::quat_cast(glm::mat3(refWorld));
		JPH::BodyCreationSettings bodySettings(res.Get(),
			JPH::RVec3(bp.x, bp.y, bp.z), JPH::Quat(bq.x, bq.y, bq.z, bq.w),
			(JPH::EMotionType)type, layer);
		bodySettings.mUserData = kGroupBodyFlag;   // mark as a group body -> pick via sub-shape data
		JPH::BodyID groupBody = bodyInterface->CreateAndAddBody(bodySettings, act);

		// The reference part owns the body (normal JoltPhysicsComponent -> ApplyPhysicsTransform
		// drives it, RemoveAllBodies frees it). Every other member follows it at its offset.
		auto& rpc = registry.emplace_or_replace<JoltPhysicsComponent>(ref);
		rpc.settings = bodySettings;
		rpc.bodyID   = groupBody;
		for (auto& [m, rel] : members) {
			if (m == ref) continue;
			auto& gm = registry.emplace_or_replace<JoltGroupMemberComponent>(m);
			gm.groupBody   = groupBody;
			gm.localOffset = rel;   // member relative to ref == relative to body origin
		}
		return groupBody;
	}

	void BGLJolt::BuildBodiesPerGroup(const std::vector<std::vector<entt::entity>>& groups, PhysicsType type, JPH::ObjectLayer layer)
	{
		for (const std::vector<entt::entity>& group : groups) {
			if (group.size() < 2) continue;   // singletons keep their own per-part body
			teardownGroupBodies(registry, *this, group);
			buildGroupCompound(group, type, layer);
		}
	}

	void BGLJolt::RebuildGroupBody(const std::vector<entt::entity>& group, PhysicsType type, JPH::ObjectLayer layer)
	{
		teardownGroupBodies(registry, *this, group);
		if (group.size() >= 2) { buildGroupCompound(group, type, layer); return; }
		if (group.size() == 1) {
			// Collapsed to a lone part: recreate a single body from its kept ColliderShapeComponent.
			entt::entity e = group.front();
			auto* tc = registry.try_get<TransformComponent>(e);
			auto* sc = registry.try_get<ColliderShapeComponent>(e);
			if (!tc || !sc || sc->shape == nullptr) return;
			const glm::mat4 w = tc->computeMat4();
			const glm::vec3 p = glm::vec3(w[3]);
			const glm::quat q = glm::quat_cast(glm::mat3(w));
			const JPH::EActivation act = (type == PhysicsType::STATIC) ? JPH::EActivation::DontActivate
			                                                          : JPH::EActivation::Activate;
			JPH::BodyCreationSettings bs(sc->shape.GetPtr(), JPH::RVec3(p.x, p.y, p.z),
				JPH::Quat(q.x, q.y, q.z, q.w), (JPH::EMotionType)type, layer);
			bs.mUserData = entt::to_integral(e);
			auto& pc = registry.emplace_or_replace<JoltPhysicsComponent>(e);
			pc.settings = bs;
			pc.bodyID   = bodyInterface->CreateAndAddBody(bs, act);
		}
	}

	entt::entity BGLJolt::resolveRaycastToBlock(JPH::BodyID body, JPH::SubShapeID subShape) const
	{
		if (body.IsInvalid()) return entt::null;
		const JPH::uint64 bu = bodyInterface->GetUserData(body);
		if (bu & kGroupBodyFlag) {
			// Group compound: the block is the TOP-LEVEL sub-shape's user data. Peel only that level
			// (GetSubShapeIndexFromID) — the brick's own nested hull compound underneath is ignored.
			JPH::RefConst<JPH::Shape> shape = bodyInterface->GetShape(body);
			if (shape != nullptr && shape->GetType() == JPH::EShapeType::Compound) {
				const JPH::CompoundShape* comp = static_cast<const JPH::CompoundShape*>(shape.GetPtr());
				JPH::SubShapeID remainder;
				JPH::uint32 idx = comp->GetSubShapeIndexFromID(subShape, remainder);
				entt::entity e{ comp->GetCompoundUserData(idx) };
				return registry.valid(e) ? e : entt::null;
			}
			return entt::null;
		}
		// Lone part (or ground): body user data is the entity directly.
		entt::entity e{ static_cast<entt::id_type>(bu) };
		return registry.valid(e) ? e : entt::null;
	}

	entt::entity BGLJolt::PickEntity(glm::vec3 origin, glm::vec3 dir, float maxDist, glm::vec3* outHit) const
	{
		JPH::RRayCast ray{ JPH::RVec3(origin.x, origin.y, origin.z),
		                   JPH::Vec3(dir.x, dir.y, dir.z) * maxDist };
		JPH::RayCastResult hit;
		if (!physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit))
			return entt::null;
		if (outHit) {
			JPH::RVec3 p = ray.GetPointOnRay(hit.mFraction);
			*outHit = { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
		}
		return resolveRaycastToBlock(hit.mBodyID, hit.mSubShapeID2);
	}

	void BGLJolt::ApplyGroupTransforms()
	{
		auto view = registry.view<TransformComponent, JoltGroupMemberComponent>();
		for (auto [entity, transComp, gm] : view.each()) {
			if (gm.groupBody.IsInvalid()) continue;
			JPH::RVec3 bpos = bodyInterface->GetPosition(gm.groupBody);
			JPH::Quat  brot = bodyInterface->GetRotation(gm.groupBody);
			glm::quat bq{ brot.GetW(), brot.GetX(), brot.GetY(), brot.GetZ() };
			glm::mat4 bodyWorld = glm::mat4_cast(bq);
			bodyWorld[3] = glm::vec4(bpos.GetX(), bpos.GetY(), bpos.GetZ(), 1.0f);

			const glm::mat4 world = bodyWorld * gm.localOffset;
			transComp.setTranslation(glm::vec3(world[3]));
			transComp.setRotation(eulerXYZFromRot(glm::mat3(world)));
		}
	}

	void BGLJolt::RehydratePhysicsBodies()
	{
		// The recipe (settings) was restored from disk but the BodyID is meaningless until
		// the live engine re-issues it. Spawn each body at its persisted world transform
		// (TransformComponent is the source of truth for pose; settings holds shape/mass/etc).
		auto spawn = [&](entt::entity ent, JPH::BodyID& bodyID, JPH::BodyCreationSettings& settings) {
			if (settings.GetShape() == nullptr) return; // never populated -> nothing to build
			if (auto* tc = registry.try_get<TransformComponent>(ent)) {
				glm::vec3 p = tc->getWorldTranslation();
				glm::vec3 r = tc->getWorldRotation();
				settings.mPosition = JPH::RVec3(p.x, p.y, p.z);
				settings.mRotation = JPH::Quat::sEulerAngles({ r.x, r.y, r.z });
			}
			bodyID = bodyInterface->CreateAndAddBody(settings, JPH::EActivation::Activate);
		};

		for (auto [ent, comp] : registry.view<JoltPhysicsComponent>().each())
			spawn(ent, comp.bodyID, comp.settings);
		for (auto [ent, comp] : registry.view<JoltKinematicComponent>().each())
			spawn(ent, comp.bodyID, comp.settings);
	}

	void BGLJolt::RemoveAllBodies()
	{
		auto destroy = [&](JPH::BodyID id) {
			if (id.IsInvalid()) return;
			bodyInterface->RemoveBody(id);
			bodyInterface->DestroyBody(id);
		};
		for (auto [ent, comp] : registry.view<JoltPhysicsComponent>().each())
			destroy(comp.bodyID);
		for (auto [ent, comp] : registry.view<JoltKinematicComponent>().each())
			destroy(comp.bodyID);
	}

	void BGLJolt::AddSphere(entt::entity ent, float radius, PhysicsBodyCreationInfo& info) {
		JPH::EActivation activity = info.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		JPH::BodyCreationSettings sphereSettings(new JPH::SphereShape(radius), JPH::RVec3(info.pos.x, info.pos.y, info.pos.z), JPH::Quat::sEulerAngles({ info.rot.x,info.rot.y,info.rot.z }), (JPH::EMotionType)info.physicsType, info.layer);
		sphereSettings.mUserData = entt::to_integral(ent);   // body -> entity (for raycast picking)
		if (info.physicsType == PhysicsType::KINEMATIC) {
			auto& jpc = registry.emplace<JoltKinematicComponent>(ent);
			jpc.settings = sphereSettings; // persisted recipe; bodyID below is transient
			jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
		}
		else {
			auto& jpc = registry.emplace<JoltPhysicsComponent>(ent);
			jpc.settings = sphereSettings; // persisted recipe; bodyID below is transient
			jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
		}
	}

	void BGLJolt::AddBox(entt::entity ent, glm::vec3 halfExtent, PhysicsBodyCreationInfo& info) {
		JPH::EActivation activity = info.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		// Use a baked BoxShape (not BoxShapeSettings) so the shape can be serialized via
		// BodyCreationSettings::SaveWithChildren when the entity is written to a map.
		JPH::BodyCreationSettings boxSettings(new JPH::BoxShape({halfExtent.x,halfExtent.y,halfExtent.z}), JPH::RVec3(info.pos.x, info.pos.y, info.pos.z), JPH::Quat::sEulerAngles({ info.rot.x,info.rot.y,info.rot.z }), (JPH::EMotionType)info.physicsType, info.layer);
		boxSettings.mUserData = entt::to_integral(ent);   // body -> entity (for raycast picking)

		if (info.physicsType == PhysicsType::KINEMATIC) {
			auto& jpc = registry.emplace<bagel::JoltKinematicComponent>(ent);
			jpc.settings = boxSettings; // persisted recipe; bodyID below is transient
			jpc.bodyID = bodyInterface->CreateAndAddBody(boxSettings, activity);
		}
		else {
			auto& jpc = registry.emplace<bagel::JoltPhysicsComponent>(ent);
			jpc.settings = boxSettings; // persisted recipe; bodyID below is transient
			jpc.bodyID = bodyInterface->CreateAndAddBody(boxSettings, activity);
		}
	}
	void BGLJolt::AddConvexHull(entt::entity ent, const std::vector<std::vector<glm::vec3>>& hulls,
	                            PhysicsBodyCreationInfo& info) {
		// Build a Jolt convex hull per point cloud. One hull -> ConvexHullShape; several ->
		// a StaticCompoundShape (Jolt requires >= 2 sub-shapes for a compound).
		auto buildHull = [](const std::vector<glm::vec3>& pts) -> JPH::RefConst<JPH::Shape> {
			JPH::Array<JPH::Vec3> jpts;
			jpts.reserve(pts.size());
			for (const glm::vec3& v : pts) jpts.push_back(JPH::Vec3(v.x, v.y, v.z));
			JPH::ConvexHullShapeSettings settings(jpts, JPH::cDefaultConvexRadius);
			JPH::ShapeSettings::ShapeResult res = settings.Create();
			if (res.HasError()) {
				CONSOLE->Log("Physics", std::string("convex hull build failed: ") + res.GetError().c_str());
				return {};
			}
			return res.Get();
		};

		JPH::RefConst<JPH::Shape> shape;
		if (hulls.size() == 1) {
			shape = buildHull(hulls[0]);
		}
		else if (hulls.size() > 1) {
			JPH::StaticCompoundShapeSettings comp;
			int added = 0;
			for (const auto& h : hulls) {
				JPH::RefConst<JPH::Shape> sub = buildHull(h);
				if (sub == nullptr) continue;
				comp.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), sub.GetPtr());
				++added;
			}
			if (added >= 2) {
				JPH::ShapeSettings::ShapeResult res = comp.Create();
				if (!res.HasError()) shape = res.Get();
				else CONSOLE->Log("Physics", std::string("compound build failed: ") + res.GetError().c_str());
			}
			else if (added == 1) {
				shape = buildHull(hulls[0]);   // degenerate compound -> fall back to a single hull
			}
		}
		if (shape == nullptr) return;   // nothing valid to add

		JPH::AABox b = shape->GetLocalBounds();
		CONSOLE->Log("Physics", "convex collider: " + std::to_string(hulls.size()) + " hull(s), bounds ["
			+ std::to_string(b.mMin.GetX()) + "," + std::to_string(b.mMin.GetY()) + "," + std::to_string(b.mMin.GetZ()) + "] .. ["
			+ std::to_string(b.mMax.GetX()) + "," + std::to_string(b.mMax.GetY()) + "," + std::to_string(b.mMax.GetZ()) + "]");

		// Keep the baked shape on the entity so a solid group can be (re)built from its members'
		// shapes later without re-baking (see buildGroupCompound / RebuildGroupBody).
		registry.emplace_or_replace<ColliderShapeComponent>(ent).shape = shape;

		JPH::EActivation activity = info.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		// Baked shape (not settings) so it serializes via BodyCreationSettings::SaveWithChildren.
		JPH::BodyCreationSettings bodySettings(shape.GetPtr(),
			JPH::RVec3(info.pos.x, info.pos.y, info.pos.z),
			JPH::Quat::sEulerAngles({ info.rot.x, info.rot.y, info.rot.z }),
			(JPH::EMotionType)info.physicsType, info.layer);
		// Tag the body with its entity so a raycast on a lone (ungrouped) part resolves directly.
		bodySettings.mUserData = entt::to_integral(ent);

		if (info.physicsType == PhysicsType::KINEMATIC) {
			auto& jpc = registry.emplace<bagel::JoltKinematicComponent>(ent);
			jpc.settings = bodySettings;
			jpc.bodyID = bodyInterface->CreateAndAddBody(bodySettings, activity);
		}
		else {
			auto& jpc = registry.emplace<bagel::JoltPhysicsComponent>(ent);
			jpc.settings = bodySettings;
			jpc.bodyID = bodyInterface->CreateAndAddBody(bodySettings, activity);
		}
	}

	glm::vec3 BGLJolt::GetGravity()
	{
		JPH::Vec3 dir = physicsSystem.GetGravity();
		return {dir.GetX(), dir.GetY(), dir.GetZ() };
	}
	void BGLJolt::SetGravity(glm::vec3 gravity)
	{
		physicsSystem.SetGravity({ gravity.x,gravity.y,gravity.z });
	}
	inline void MyContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
	{
		auto v = inManifold.GetWorldSpaceContactPointOn1(0);
		std::string contactSTR = "Contact on ";
		char storage[32];
		sprintf(storage, " %.2f %.2f %.2f", v.GetX(), v.GetY(), v.GetZ());
		contactSTR += std::string(storage);
		CONSOLE->Log("BGLJolt", contactSTR);
		CONSOLE->Log("BGLJolt", "A contact was added");
	}
	inline void MyBodyActivationListener::OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData)
	{
		CONSOLE->Log("BGLJolt", "Body Activated");
	}
	inline void MyBodyActivationListener::OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData)
	{
		CONSOLE->Log("BGLJolt", "A body went to sleep");
	}
}