#include "bagel_jolt/bagel_jolt.hpp"
#include "bagel_ecs_components.hpp"

namespace bagel {
	BGLJolt* BGLJolt::instance = nullptr;

	BGLJolt::BGLJolt(entt::registry& r) : registry{ r }{
		physicsSystem.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);
		physicsSystem.SetBodyActivationListener(&body_activation_listener);
		physicsSystem.SetContactListener(&contact_listener);
	
		// The main way to interact with the bodies in the physics system is through the body interface. There is a locking and a non-locking
		// variant of this. We're going to use the locking version (even though we're not planning to access bodies from multiple threads)
		bodyInterface = &physicsSystem.GetBodyInterface();

		// Next we can create a rigid body to serve as the floor, we make a large box
		// Create the settings for the collision volume (the shape).
		// Note that for simple shapes (like boxes) you can also directly construct a BoxShape.
		//BoxShapeSettings floor_shape_settings(Vec3(100.0f, 1.0f, 100.0f));

		// Create the shape
		//ShapeSettings::ShapeResult floor_shape_result = floor_shape_settings.Create();
		//ShapeRefC floor_shape = floor_shape_result.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

		// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
		//BodyCreationSettings floor_settings(floor_shape, RVec3(0.0_r, -1.0_r, 0.0_r), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);

		// Create the actual rigid body
		//Body* floor = bodyInterface->CreateBody(floor_settings); // Note that if we run out of bodies this can return nullptr

		// Add it to the world
		//bodyInterface->AddBody(floor->GetID(), EActivation::DontActivate);

		// Now create a dynamic body to bounce on the floor
		// Note that this uses the shorthand version of creating and adding a body to the world
		//BodyCreationSettings sphere_settings(new SphereShape(0.5f), RVec3(0.0_r, 2.0_r, 0.0_r), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
		//BodyID sphere_id = bodyInterface->CreateAndAddBody(sphere_settings, EActivation::Activate);

		// Now you can interact with the dynamic body, in this case we're going to give it a velocity.
		// (note that if we had used CreateBody then we could have set the velocity straight on the body before adding it to the physics system)
		//bodyInterface->SetLinearVelocity(sphere_id, Vec3(0.0f, -5.0f, 0.0f));
	
		// Optional step: Before starting the physics simulation you can optimize the broad phase. This improves collision detection performance (it's pointless here because we only have 2 bodies).
		// You should definitely not call this every frame or when e.g. streaming in a new level section as it is an expensive operation.
		// Instead insert all new objects in batches instead of 1 at a time to keep the broad phase efficient.
		physicsSystem.OptimizeBroadPhase();
	}

	BGLJolt::~BGLJolt()
	{
		// Remove the sphere from the physics system. Note that the sphere itself keeps all of its state and can be re-added at any time.
		// body_interface.RemoveBody(sphere_id);

		// Destroy the sphere. After this the sphere ID is no longer valid.
		// body_interface.DestroyBody(sphere_id);

		// Remove and destroy the floor
		// body_interface.RemoveBody(floor->GetID());
		// body_interface.DestroyBody(floor->GetID());

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
			physicsSystem.Update(dt, cCollisionSteps, &tempAllocator, &job_system);
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

			transComp.setTranslation(newVec);
			transComp.setRotation(newRot);
		}
	}

	void BGLJolt::AddSphere(entt::entity ent, float radius, glm::vec3 pos, glm::vec3 rot, bool isDynamic, bool activate, JPH::ObjectLayer layer) {
		JPH::EMotionType type = isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
		JPH::EActivation activity = activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		JPH::BodyCreationSettings sphereSettings(new JPH::SphereShape(radius), JPH::RVec3(pos.x, pos.y, pos.z), JPH::Quat::sEulerAngles({ rot.x,rot.y,rot.z }), type, layer);

		auto& jpc = registry.emplace<bagel::JoltPhysicsComponent>(ent);
		//jpc.AddBody(bodyInterface->CreateAndAddBody(sphereSettings, activity));
		jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
	}

	void BGLJolt::AddBox(entt::entity ent, glm::vec3 halfExtent, glm::vec3 pos, glm::vec3 rot, bool isDynamic, bool activate, JPH::ObjectLayer layer) {
		JPH::EMotionType type = isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
		JPH::EActivation activity = activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		JPH::BodyCreationSettings sphereSettings(new JPH::BoxShapeSettings({halfExtent.x,halfExtent.y,halfExtent.z}), JPH::RVec3(pos.x, pos.y, pos.z), JPH::Quat::sEulerAngles({ rot.x,rot.y,rot.z }), type, layer);
	
		auto& jpc = registry.emplace<bagel::JoltPhysicsComponent>(ent);
		//jpc.AddBody(bodyInterface->CreateAndAddBody(sphereSettings, activity));
		jpc.bodyID = bodyInterface->CreateAndAddBody(sphereSettings, activity);
	}
}