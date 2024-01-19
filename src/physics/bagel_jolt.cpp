#include "physics/bagel_jolt.hpp"
#include "bagel_ecs_components.hpp"
#include "../bgl_model.hpp"
#include "../bagel_util.hpp"

#include "bagel_imgui.hpp"
#define CONSOLE ConsoleApp::Instance()

namespace bagel {
	BGLJolt* BGLJolt::instance = nullptr;

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
		auto& viewPhysics = registry.view<JoltPhysicsComponent>();
		for (auto [ent,comp] : viewPhysics.each()) {
			if (activity) bodyInterface->ActivateBody(comp.bodyID);
			else bodyInterface->DeactivateBody(comp.bodyID);
		}
		auto& viewKinematic = registry.view<JoltKinematicComponent>();
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
		auto entView = registry.view<TransformComponent, JoltPhysicsComponent>();
		for (auto [ entity, transComp, physComp ] : entView.each()) {

			JPH::RVec3 newPos = bodyInterface->GetPosition(physComp.bodyID);
			JPH::Quat newQuat = bodyInterface->GetRotation(physComp.bodyID);

			// Rotation order: RotZ * RotY * RotX * point;
			glm::vec3 newVec = { newPos.GetX(), newPos.GetY(),newPos.GetZ() };
			JPH::RVec3 newEuler = newQuat.GetEulerAngles();
			glm::vec3 newRot = { newEuler.GetX(), newEuler.GetY(), newEuler.GetZ() };

			glm::qua<float> q{ newQuat.GetW(), newQuat.GetX(),newQuat.GetY(), newQuat.GetZ() };
			glm::vec3 gvec = glm::eulerAngles(q);

			transComp.setTranslation(newVec);
			transComp.setRotation(newRot);
		}
	}

	void BGLJolt::AddSphere(entt::entity ent, float radius, PhysicsBodyCreationInfo& info) {
		JPH::EActivation activity = info.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		JPH::BodyCreationSettings sphereSettings(new JPH::SphereShape(radius), JPH::RVec3(info.pos.x, info.pos.y, info.pos.z), JPH::Quat::sEulerAngles({ info.rot.x,info.rot.y,info.rot.z }), (JPH::EMotionType)info.physicsType, info.layer);
		if (info.physicsType == PhysicsType::KINEMATIC) {
			auto& jpc = registry.emplace<JoltKinematicComponent>(ent);
			jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
		}
		else {
			auto& jpc = registry.emplace<JoltPhysicsComponent>(ent);
			jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
		}
		
		//Create collision model
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		CollisionModelComponent& wfc = modelBuilder->buildComponent<CollisionModelComponent>(ent, "/models/wiresphere.obj", ComponentBuildMode::LINES);
		wfc.collisionScale = {radius,radius,radius};
		delete modelBuilder;
	}

	void BGLJolt::AddBox(entt::entity ent, glm::vec3 halfExtent, PhysicsBodyCreationInfo& info) {
		JPH::EActivation activity = info.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		JPH::BodyCreationSettings sphereSettings(new JPH::BoxShapeSettings({halfExtent.x,halfExtent.y,halfExtent.z}), JPH::RVec3(info.pos.x, info.pos.y, info.pos.z), JPH::Quat::sEulerAngles({ info.rot.x,info.rot.y,info.rot.z }), (JPH::EMotionType)info.physicsType, info.layer);
		
		if (info.physicsType == PhysicsType::KINEMATIC) {
			auto& jpc = registry.emplace<bagel::JoltKinematicComponent>(ent);
			jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
		}
		else {
			auto& jpc = registry.emplace<bagel::JoltPhysicsComponent>(ent);
			jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
		}

		//Create collision model
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		CollisionModelComponent& wfc = modelBuilder->buildComponent<CollisionModelComponent>(ent, "/models/wirecube.obj", ComponentBuildMode::LINES);
		wfc.collisionScale = glm::vec3({ halfExtent.x,halfExtent.y,halfExtent.z });
		delete modelBuilder;
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
		auto& v = inManifold.GetWorldSpaceContactPointOn1(0);
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