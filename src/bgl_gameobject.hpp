#pragma once

#include <glm/gtc/matrix_transform.hpp>


#include "bgl_model.hpp"
#include <unordered_map>
#include <memory>
namespace bagel {
	struct TransformComponent {
		glm::vec3 translation{};
		glm::vec3 scale{ 1.f,1.f,1.f };
		glm::vec3 rotation;
		glm::mat4 mat4();
		glm::mat3 normalMatrix();
	};
	struct PointLightComponent {
		float lightIntensity = 1.0f;
	};
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

		std::shared_ptr<BGLModel> model{};
		glm::vec3 color{};
		//TransformComponent transform{};
		void addTransformComponent(TransformComponent tr) { transforms.push_back(tr); }
		void createDefaultTransform() { transforms.push_back(TransformComponent{}); }
		std::vector<TransformComponent> transforms{};
		uint32_t transformCount = 1;

		std::unique_ptr<PointLightComponent> pointLight = nullptr;

	private:
		BGLGameObject(id_t objectId) : id{ objectId } {};
		id_t id;
	};
}