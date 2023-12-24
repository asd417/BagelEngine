#include "bagel_physics.h"
namespace bagel {
namespace Physics {
	// Moved to jolt physics engine. Will revisit later
#ifdef PHYSTEST
	void CollisionSphereSphere(PhysicsComponent& pA, PhysicsComponent& pB, SphereColliderComponent& cA, SphereColliderComponent& cB, TransformComponent& tA, TransformComponent& tB) {
		for (int ai = 0; ai < cA.colliderCount; ai++) {
			for (int bi = 0; bi < cB.colliderCount; bi++) {
				//depth is vector from cA to cB
				glm::vec3 worldCenterA = tA.mat4() * glm::vec4(cA.center[ai], 1.0);
				//glm::vec3 worldCenterA = tA.translation + cA.center[ai];
				//glm::vec3 worldCenterB = tA.translation + cA.center[ai];
				glm::vec3 worldCenterB = tB.mat4() * glm::vec4(cB.center[bi], 1.0);
				glm::vec3 depth = worldCenterA - worldCenterB;
				std::cout << "worldCenterA: " << worldCenterA.x << " " << worldCenterA.y << " " << worldCenterA.z << "\n";
				std::cout << "worldCenterB: " << worldCenterB.x << " " << worldCenterB.y << " " << worldCenterB.z << "\n";
				if (depth.length() <= cA.radius[ai] + cB.radius[bi]) {
					std::cout << "Collision Detected: ";
					// Coefficient of restitution
					float Cr = sqrt(pA.bounciness * pB.bounciness);
					float addM = (pA.mass + pB.mass);
					pA.frameVelocity += (pA.mass * pA.velocity + pB.mass * pB.velocity + pB.mass * Cr * (pB.velocity - pA.velocity)) / addM;
					pB.frameVelocity += (pA.mass * pA.velocity + pB.mass * pB.velocity + pA.mass * Cr * (pA.velocity - pB.velocity)) / addM;
				}
			}
		}
	}
	void CollisionSpherePlane(PhysicsComponent& pA, PhysicsComponent& pB, SphereColliderComponent& cA, PlaneColliderComponent& cB) {}
	void CollisionSphereCapsule(PhysicsComponent& pA, PhysicsComponent& pB, SphereColliderComponent& cA, CapsuleColliderComponent& cB) {}
	void CollisionPlanePlane(PhysicsComponent& pA, PhysicsComponent& pB, PlaneColliderComponent& cA, PlaneColliderComponent& cB) {}
	void CollisionPlaneCapsule(PhysicsComponent& pA, PhysicsComponent& pB, PlaneColliderComponent& cA, CapsuleColliderComponent& cB) {}
	void CollisionCapsuleCapsule(PhysicsComponent& pA, PhysicsComponent& pB, CapsuleColliderComponent& cA, CapsuleColliderComponent& cB) {}

	PhysicsSystem::PhysicsSystem(entt::registry& _registry) : registry{_registry}
	{
		//registry.group<PhysicsComponent>();
	}
	void PhysicsSystem::Step(float dt) {
		auto physView = registry.view<PhysicsComponent>();
		std::cout << "Physics: ";
		for (auto entity : physView) {
			auto& physComp = physView.get<PhysicsComponent>(entity);
			physComp.velocity = physComp.frameVelocity;
			physComp.frameVelocity = { 0,0,0 };
			//Apply gravity
			if (physComp.gravity) {
				glm::vec3 gravForce = physComp.mass * gravity; 
				physComp.velocity += gravForce / physComp.mass * dt;
				//std::cout << "velocity: " << physComp.velocity.x << " " << physComp.velocity.y << " " << physComp.velocity.z << " " << "\n";
			}

			auto& transformComp = registry.get<TransformComponent>(entity);
			transformComp.translation += physComp.velocity * dt;
			std::cout << "(" << transformComp.translation.x << " " << transformComp.translation.y << " " << transformComp.translation.z << ") " << " ";
		}
		std::cout << "\n";
	};
	void PhysicsSystem::TestCollision()
	{
		auto physView = registry.view<PhysicsComponent>();
		//auto group = registry.group<PhysicsComponent>();
		// Loop through all combination of entity pair
		for (auto it = physView.begin(); it != physView.end(); it++) {
			auto entityA = *it;
			auto& physCompA = registry.get<PhysicsComponent>(entityA);
			//auto& transCompA = group.get<TransformComponent>(entityA);
			auto& transCompA = registry.get<TransformComponent>(entityA);

			for (auto it2 = it+1; it2 != physView.end(); it2++) {
				auto entityB = *it2;

				if (entityA == entityB) {
					std::cout << "A B Same\n";
					continue;
				}
				
				auto& physCompB = registry.get<PhysicsComponent>(entityB);
				//auto& transCompB = group.get<TransformComponent>(entityB);
				auto& transCompB = registry.get<TransformComponent>(entityB);

				bool IsSphereA =	physCompA.colliderTypeFlag & ColliderFlag::SPHERE;
				bool IsSphereB =	physCompB.colliderTypeFlag & ColliderFlag::SPHERE;
				bool IsPlaneA =		physCompA.colliderTypeFlag & ColliderFlag::PLANE;
				bool IsPlaneB =		physCompB.colliderTypeFlag & ColliderFlag::PLANE;
				bool IsCapsuleA =	physCompA.colliderTypeFlag & ColliderFlag::CAPSULE;
				bool IsCapsuleB =	physCompB.colliderTypeFlag & ColliderFlag::CAPSULE;

				if (IsSphereA && IsSphereB) {
					auto& sphereA = registry.get<SphereColliderComponent>(entityA);
					auto& sphereB = registry.get<SphereColliderComponent>(entityB);

					CollisionSphereSphere(physCompA, physCompB, sphereA, sphereB, transCompA, transCompB);
				}
				if (IsSphereA && IsPlaneB) {
					auto& sphereA = registry.get<SphereColliderComponent>(entityA);
					auto& planeB = registry.get<PlaneColliderComponent>(entityB);
					CollisionSpherePlane(physCompA, physCompB, sphereA, planeB);
				}
				if (IsSphereA && IsCapsuleB) {
					auto& sphereA =	registry.get<SphereColliderComponent>(entityA);
					auto& capsuleB = registry.get<CapsuleColliderComponent>(entityB);
					CollisionSphereCapsule(physCompA, physCompB, sphereA, capsuleB);
				}
				if (IsPlaneA && IsPlaneB) {
					auto& planeA = registry.get<PlaneColliderComponent>(entityA);
					auto& planeB = registry.get<PlaneColliderComponent>(entityB);
					CollisionPlanePlane(physCompA, physCompB, planeA, planeB);
				}
				if (IsPlaneA && IsCapsuleB) {
					auto& planeA = registry.get<PlaneColliderComponent>(entityA);
					auto& capsuleB = registry.get<CapsuleColliderComponent>(entityB);
					CollisionPlaneCapsule(physCompA, physCompB, planeA, capsuleB);
				}
				if (IsCapsuleA && IsCapsuleB) {
					auto& capsuleA = registry.get<CapsuleColliderComponent>(entityA);
					auto& capsuleB = registry.get<CapsuleColliderComponent>(entityB);
					CollisionCapsuleCapsule(physCompA, physCompB, capsuleA, capsuleB);
				}
			}
		}
		
	}
#endif


}
}
