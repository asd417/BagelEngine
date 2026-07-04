#pragma once
// Debug component: the connection points (studs / holes) detected for a LEGO part,
// stored in the part's MODEL-LOCAL space (i.e. already multiplied by the load scale,
// so they line up with the vertex buffer). GizmoRenderSystem::renderConnections draws
// a ring + axis marker at each, transformed by the entity's TransformComponent.
//
// POD + glm only on purpose: the render system includes this without dragging in the
// ldraw parser. loadLegoBrick() fills it from bagel::ldraw::ConnectionPoint.

#include <vector>
#include <glm/glm.hpp>

namespace bagel {

	struct LegoConnectionPoint {
		glm::vec3 pos{ 0.0f };     // model-local position (raw LDU * loadScale)
		glm::mat3 orient{ 1.0f };  // basis; column 1 (+Y) is the connector axis
		float     radius = 0.6f;   // model-local marker radius
		int       type = 0;        // 0 = male stud, 1 = female tube, 2 = pin hole, 3 = axle hole
	};

	struct LegoConnectionComponent {
		std::vector<LegoConnectionPoint> points;
	};

} // namespace bagel
