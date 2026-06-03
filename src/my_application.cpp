#include "my_application.hpp"
#include <iostream>

namespace bagel {

	void MyApplication::OnSceneLoad()
	{
		createLights();
		placeCubes();
		placePurpleCube();
	}

	void MyApplication::OnUpdate(float dt)
	{
	}

	void MyApplication::placeCubes()
	{
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);

		Material bricksMat = materialManager->loadMaterial(
			"/materials/Bricks089_1K-PNG_Color.png",
			"/materials/Bricks089_1K-PNG_NormalGL.png",
			"/materials/Bricks089_1K-PNG_Roughness.png");

		struct CubeDef { glm::vec3 pos; glm::vec3 scale; };
		CubeDef defs[] = {
			{{ 2.0f,  0.0f,  0.0f}, {0.4f, 0.4f, 0.4f}},
			{{-2.0f,  0.0f,  0.0f}, {0.4f, 0.4f, 0.4f}},
			{{ 0.0f,  0.0f,  2.0f}, {0.4f, 0.4f, 0.4f}},
			{{ 0.0f,  0.0f, -2.0f}, {0.4f, 0.4f, 0.4f}},
			{{ 1.5f,  0.6f,  1.5f}, {0.3f, 0.3f, 0.3f}},
			{{-1.5f,  0.6f,  1.5f}, {0.3f, 0.3f, 0.3f}},
			{{ 1.5f,  0.6f, -1.5f}, {0.3f, 0.3f, 0.3f}},
			{{-1.5f,  0.6f, -1.5f}, {0.3f, 0.3f, 0.3f}},
		};

		for (auto& def : defs) {
			auto entity = registry.create();
			auto& tfc = registry.emplace<TransformComponent>(entity);
			tfc.setTranslation(def.pos);
			tfc.setScale(def.scale);
			auto& model = modelBuilder->buildComponent<ModelComponent>(entity, "/models/cube.obj", ComponentBuildMode::FACES);
			model.setMaterial(0, bricksMat);
		}

		delete modelBuilder;
	}

	void MyApplication::placePurpleCube()
	{
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);

		Material purpleMat = materialManager->loadMaterial(
			"/materials/purple_albedo.png",
			nullptr, nullptr, nullptr,
			"/materials/purple_emission.png");

		auto entity = registry.create();
		auto& tfc = registry.emplace<TransformComponent>(entity);
		tfc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tfc.setScale({ 0.3f, 0.3f, 0.3f });
		auto& model = modelBuilder->buildComponent<ModelComponent>(entity, "/models/cube.obj", ComponentBuildMode::FACES);
		model.setMaterial(0, purpleMat);

		delete modelBuilder;
	}

	void MyApplication::createLights()
	{
		std::vector<glm::vec3> lightColors{
			 {1.f, .1f, .1f},
			 {.1f, .1f, 1.f},
			 {.1f, 1.f, .1f},
			 {1.f, 1.f, .1f},
			 {.1f, 1.f, 1.f},
			 {1.f, 1.f, 1.f}
		};
		for (int i = 0; i < (int)lightColors.size(); i++) {
			auto rot = glm::rotate(
				glm::mat4(1.0f),
				(i * glm::two_pi<float>() / lightColors.size()),
				{ 0.f,-1.f,0.f });
			const auto entity = registry.create();
			registry.emplace<TransformComponent>(entity, (rot * glm::vec4(glm::vec3{ 3.f,1.0f,0.0f }, 1.0f)));
			registry.emplace<InfoComponent>(entity);
			auto& light = registry.emplace<PointLightComponent>(entity);
			light.color = glm::vec4(lightColors[i], 4.0f);
		}
	}

} // namespace bagel

int main() {
	bagel::MyApplication app{};
	try {
		app.run();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
