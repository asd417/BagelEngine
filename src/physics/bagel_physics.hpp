#pragma once

#include <glm/glm.hpp>
#include "entt.hpp"

#include "bagel_ecs_components.hpp"

namespace bagel {
	namespace Physics {
		enum ColliderFlag {
			SPHERE	 = 0b0000'0001,
			PLANE	 = 0b0000'0010,
			CAPSULE	 = 0b0000'0100,
			MESH	 = 0b0000'1000
		};

		struct CollisionPoints {
			glm::vec3 A; // Furthest point of A into B
			glm::vec3 B; // Furthest point of B into A
			glm::vec3 Normal; // B – A normalized
			float Depth; // Length of B – A
			bool HasCollision;
		};

		class PhysicsSystem {
		public:
			PhysicsSystem(entt::registry& _registry);
			~PhysicsSystem() = default;
			PhysicsSystem operator=(PhysicsSystem&) = delete;
			void Step(float dt);
			void TestCollision();

		private:
			entt::registry& registry;
			glm::vec3 gravity = { 0, 9.81f,0 };
		};
	}
}