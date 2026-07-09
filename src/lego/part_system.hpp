#pragma once
// Spawns placeable LEGO parts into the scene. This is the "lazy geometry" half of the
// catalog + lazy geometry model (see part_catalog.hpp): PartCatalog only proves a part is
// placeable, PartSystem does the actual (expensive) mesh / connector / collision load the
// first time a part is spawned. The baked-asset caches live here as members, so every part
// pays its parse cost exactly once per system instance.
//
// Holds references to the engine services a spawn needs; construct it after the owning
// Application has built its material + skin managers.

#include "baked_collision.hpp"   // BakedCollision (offline convex-hull cache)
#include "baked_connectors.hpp"  // BakedConnectors (offline connector cache)

#include "entt.hpp"
#include <glm/glm.hpp>

#include <string>

namespace bagel {

	class BGLDevice;
	class BGLMaterialManager;
	class BGLSkinManager;

	namespace ldraw {

		class PartSystem {
		public:
			PartSystem(BGLDevice& device,
					   entt::registry& registry,
					   BGLMaterialManager& materialManager,
					   BGLSkinManager& skinManager);

			// Spawn one LDraw part as a new entity at `pos`. settings.scale bakes the raw LDU geometry
			// (1 LDU = 0.4 mm) down to engine size at load time. LDraw is -Y up, so a rigid 180° flip
			// about X puts the studs up (a pure rotation keeps winding/normals correct). Attaches the
			// baked connectors (gizmo markers) and the convex-hull collider. Does NOT clear the scene,
			// so it can add bricks to whatever is already loaded (used by the part browser).
			entt::entity spawnLegoPart(const std::string& partName, const glm::vec3& pos);

		private:
			BGLDevice&          device_;
			entt::registry&     registry_;
			BGLMaterialManager& materialManager_;
			BGLSkinManager&     skinManager_;

			BakedConnectors connectorCache_;  // lego/baked/connections/<part>.conn
			BakedCollision  collisionCache_;  // lego/baked/collision/<part>.glb
		};

	} // namespace ldraw
} // namespace bagel
