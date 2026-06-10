#include "my_application.hpp"
#include "bagel_hierachy.hpp"
#include <iostream>
#include <cmath>

namespace bagel {

	void MyApplication::OnSceneLoad()
	{
		createLights();
		//placeCubes();
		//placePurpleCube();
		//buildHierarchyStack();
		loadSponza();
	}

	void MyApplication::OnUpdate(float dt)
	{
		if (hierarchyRoot == entt::null) return;
		stackAngle += dt;

		auto& tc = registry.get<TransformComponent>(hierarchyRoot);
		tc.setTranslation({
			8.0f * cosf(stackAngle * 0.4f),
			1.0f + 0.5f * sinf(stackAngle * 0.7f),
			8.0f * sinf(stackAngle * 0.4f)
		});
		tc.setRotation({
			sinf(stackAngle * 0.5f) * 0.3f,
			stackAngle * 0.8f,
			cosf(stackAngle * 0.3f) * 0.2f
		});
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
			ModelLoadSettings settings{};
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
			nullptr, nullptr,
			"/materials/purple_emission.png");

		auto entity = registry.create();
		auto& tfc = registry.emplace<TransformComponent>(entity);
		tfc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tfc.setScale({ 0.3f, 0.3f, 0.3f });
		ModelLoadSettings settings{};
		auto &model = modelBuilder->buildComponent<ModelComponent>(entity, "/models/cube.obj", settings);
		model.setMaterial(0, purpleMat);

		delete modelBuilder;
	}

	void MyApplication::buildHierarchyStack()
	{
		const int   COUNT        = 1000;
		const float cubeScale    = 0.2f;
		const float localYOffset = 1.5f;   // in parent-local units; world offset = cubeScale * 1.5 = 0.3
		const float twistPerLevel = glm::radians(8.0f);

		auto* builder = new ModelComponentBuilder(bglDevice, registry);
		HierachySystem hs(registry);

		ModelLoadSettings settings{};
		settings.scaleVec = { 1.0f, 1.0f, 1.0f };

		entt::entity prev = entt::null;
		for (int i = 0; i < COUNT; i++) {
			entt::entity e = registry.create();
			auto& tc = registry.emplace<TransformComponent>(e);
			tc.setScale({ cubeScale, cubeScale, cubeScale });
			builder->buildComponent<ModelComponent>(e, "/models/cube.obj", settings);

			if (i == 0) {
				tc.setTranslation({ 8.0f, 0.0f, 0.0f });
				hierarchyRoot = e;
			} else {
				hs.CreateHierachy(prev, e);
				auto& hier = registry.get<TransformHierachyComponent>(e);
				hier.localTranslation = { 0.0f, localYOffset, 0.0f };
				hier.localRotation    = { 0.0f, twistPerLevel, 0.0f };
				hier.localScale       = { 1.0f, 1.0f, 1.0f };
			}
			prev = e;
		}

		delete builder;
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
			light.color = glm::vec4(lightColors[i], 1.0f);
			light.lux = 800.0f;
		}
		{
			const auto entity = registry.create();
			glm::vec4 color = {0.6f, 0.6f, 0.2f, 1.0f};
			glm::vec3 rotation = {-64.0f, 30.0f, 0.0f};
			auto& sun = registry.emplace<DirectionalLightComponent>(entity);
			sun.color = color;
			sun.rotation = rotation;
			sun.shadowBiasMin = 0.0f;
			sun.shadowBiasSlope = 0.0f;
		}
	}

	void MyApplication::loadSponza()
	{
		auto* builder = new ModelComponentBuilder(bglDevice, registry);
		builder->setTextureLoader(&materialManager->getTextureLoader());

		auto entity = registry.create();
		auto& tc = registry.emplace<TransformComponent>(entity);
		tc.setTranslation({ 0.0f, 0.0f, 0.0f });
		tc.setScale({ 0.01f, 0.01f, 0.01f });

		ModelLoadSettings settings{};
		// Sponza is large: keep each source mesh's solid geometry as its own submesh
		// (instead of merging into one) so per-submesh frustum culling can skip
		// off-screen chunks. Set to true to merge back into a single opaque submesh.
		settings.mergeSolidSubmeshes = false;
		builder->buildComponent<ModelComponent>(entity, "/models/sponza/Sponza.gltf", settings);

		delete builder;
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
