#pragma once
#include "bagel_application.hpp"
#include "bagel_material.hpp"

namespace bagel {

	class MyApplication : public Application {
	public:
		void OnSceneLoad() override;
		void OnUpdate(float dt) override;

	private:
		void placeCubes();
		void placePurpleCube();
		void createLights();
		void buildHierarchyStack();
		void loadSponza();

		entt::entity hierarchyRoot = entt::null;
		float stackAngle = 0.0f;
	};

} // namespace bagel
