#include "part_system.hpp"

#include "imgui/bagel_imgui.hpp" // ConsoleApp (the CONSOLE macro lives in application/, not reachable here)
#include "bagel_material.hpp"    // BGLMaterialManager
#include "model/model_component_builder.hpp" // ModelComponentBuilder
#include "lego_model_builder.hpp" // LegoModelComponentBuilder (.dat -> LDrawModelLoader)
#include "bagel_util.hpp"        // util::enginePath
#include "animation/bagel_skin_manager.hpp"
#include "ecs/components/transform.hpp"
#include "model/model_loaders/model_load_settings.hpp"
#include "physics/bagel_jolt.hpp"

#include "components/lego_connection_component.hpp"
#include "ldraw_library.hpp" // ldraw::Library (live connector re-bake on cache miss)

#include <glm/gtc/constants.hpp>

#include <string>
#include <utility>
#include <vector>

namespace bagel::ldraw {

	PartSystem::PartSystem(BGLDevice& device,
						   entt::registry& registry,
						   BGLMaterialManager& materialManager,
						   BGLSkinManager& skinManager)
		: device_{device}, registry_{registry}, materialManager_{materialManager}, skinManager_{skinManager}
	{
		connectorCache_.setDir(util::enginePath("/lego/baked/connections"));
		collisionCache_.setDir(util::enginePath("/lego/baked/collision"));
	}

	entt::entity PartSystem::spawnLegoPart(const std::string& partName, const glm::vec3& pos)
	{
		LegoModelComponentBuilder builder(device_, registry_);
		builder.setTextureLoader(&materialManager_.getTextureLoader());
		builder.setMaterialManager(&materialManager_);
		builder.setSkinManager(&skinManager_);

		ModelLoadSettings settings{};
		settings.scale = 0.01f;

		const std::string part = partName + ".dat"; // ".dat" ext routes to LDrawModelLoader
		auto entity = registry_.create();
		auto& tc = registry_.emplace<TransformComponent>(entity);
		tc.setTranslation({0, 5, 0});
		tc.setRotation({glm::pi<float>(), 0.0f, 0.0f});
		// settings.scale already bakes the LDU geometry to engine size, so the transform must be
		// identity scale. The TransformComponent default is 0.1 (see transform.hpp); leaving it
		// would scale the RENDER mesh a second time (0.1*0.1) while the Jolt collider — which
		// ignores transform scale — stays at the bake size, so the two would mismatch 10x.
		tc.setScale({1.0f, 1.0f, 1.0f});
		builder.buildComponent(entity, part.c_str(), settings);

		// Connectors (offline lego/baked/connections/<part>.conn; live re-bake on cache miss)
		// -> gizmo markers, stored in model-local space (raw LDU * loadScale).
		std::vector<ConnectionPoint> liveBake; // holds fallback result, if used
		const std::vector<ConnectionPoint>* conns = connectorCache_.find(part);
		if (!conns)
		{
			Library lib(util::enginePath("/lego/ldraw"));
			liveBake = std::move(lib.bake(part).connections);
			conns = &liveBake;
			ConsoleApp::Instance()->Log("Lego", std::string("Connector cache miss for ") + part + " - re-baked live");
		}
		auto& cc = registry_.emplace<LegoConnectionComponent>(entity);
		auto radiusFor = [](ConnectorType t)
		{
			switch (t)
			{
			case ConnectorType::AxleHole:
				return 5.0f; // cross hole, a touch tighter
			default:
				return 6.0f; // stud / tube / pin ~ 6 LDU
			}
		};
		for (const ConnectionPoint& c : *conns)
		{
			LegoConnectionPoint p;
			p.pos = c.pos * settings.scale;
			p.orient = c.orient;
			p.radius = radiusFor(c.type) * settings.scale;
			p.type = static_cast<int>(c.type);
			cc.points.push_back(p);
		}

		// Convex-hull collider (offline lego/baked/collision/<part>.glb). Hull points are raw LDU
		// (studs-down, like the render mesh pre-transform), so scale them the same; the studs-up
		// flip lives in the entity's TransformComponent rotation handed to Jolt.
		if (const auto* hulls = collisionCache_.find(part))
		{
			std::vector<std::vector<glm::vec3>> scaled;
			scaled.reserve(hulls->size());
			for (const auto& h : *hulls)
			{
				std::vector<glm::vec3> s;
				s.reserve(h.size());
				for (const glm::vec3& v : h)
					s.push_back(v * settings.scale);
				scaled.push_back(std::move(s));
			}
			BGLJolt::PhysicsBodyCreationInfo info{};
			info.pos = tc.getWorldTranslation();
			info.rot = tc.getWorldRotation();
			info.physicsType = PhysicsType::DYNAMIC; // bricks fall + collide (ground is STATIC)
			info.activate = true;
			info.layer = PhysicsLayers::GROUND_ONLY; // collide with ground only, not each other
			BGLJolt::GetInstance()->AddConvexHull(entity, scaled, info);
		}
		ConsoleApp::Instance()->Log("Lego", "Spawned " + partName);
		return entity;
	}

} // namespace bagel::ldraw
