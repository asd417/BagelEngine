#pragma once

#include <glm/gtc/matrix_transform.hpp>

#include "bagel_ecs_components.hpp"
#include "bgl_model.hpp"
#include <unordered_map>
#include <memory>
namespace bagel {
	
	class BGLGameObject {
	public:
		using id_t = unsigned int;
		using Map = std::unordered_map<id_t, BGLGameObject>;

		static BGLGameObject createGameObject() {
			static id_t currentId = 0;
			return BGLGameObject(currentId++);
		}
		static BGLGameObject makePointLight(float intensity = 10.f, float radius = 0.5f, glm::vec3 color = glm::vec3(1.0f));

		id_t getId() const { return id; }

		BGLGameObject(const BGLGameObject&) = delete;
		BGLGameObject& operator=(const BGLGameObject&) = delete;

		BGLGameObject(BGLGameObject&&) = default;
		BGLGameObject& operator=(BGLGameObject&&) = default;

		glm::vec3 color{};
		//TransformComponent transform{};
		//void addTransformComponent(TransformComponent tr) { transform.push_back(tr); }
		//void createDefaultTransform() { TransformComponent{}; }
		TransformComponent transform{};
		uint32_t transformCount = 1;

		std::unique_ptr<PointLightComponent> pointLight = nullptr;

	private:
		BGLGameObject(id_t objectId) : id{ objectId } {};
		id_t id;
	};
}