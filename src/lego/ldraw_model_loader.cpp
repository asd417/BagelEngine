#include "ldraw_model_loader.hpp"
#include "../bagel_util.hpp"

#include <iostream>

namespace bagel {

	void LDrawModelLoader::load(ModelLoadSettings parms) {
		// LDraw library root: <engine>/lego/ldraw (holds parts/ and p/).
		static const std::string root = util::enginePath("/lego/ldraw");
		ldraw::Library lib(root);

		ldraw::BakeResult baked = lib.bake(parms.source);
		if (baked.mesh.positions.empty()) {
			std::cerr << "[LDrawModelLoader] '" << parms.source
			          << "' produced no geometry (" << lib.unresolvedCount()
			          << " unresolved refs)\n";
		}
		// Count before moving — the count helpers read baked.connections.
		const int males = baked.maleCount();
		const int females = baked.femaleCount();
		const int pins = baked.pinCount();
		const int axles = baked.axleCount();
		connections_ = std::move(baked.connections);

		// Set up the (single, default) material slot so the skin/material system is
		// consistent with the other loaders. LDraw color codes are a later concern.
		buildSkinBlock(parms.source);

		const glm::vec3 s = parms.scaleVec * parms.scale;
		const glm::vec3 defaultColor{ 0.8f, 0.8f, 0.8f };

		// bake() emits parallel positions/normals with an index buffer referencing them.
		// Copy straight across — the mesh is already indexed; no re-welding needed.
		vertices.reserve(baked.mesh.positions.size());
		for (size_t i = 0; i < baked.mesh.positions.size(); ++i) {
			BGLModel::Vertex v{};
			v.position = baked.mesh.positions[i] * s;
			v.normal   = baked.mesh.normals[i];
			v.color    = defaultColor;
			v.uv       = glm::vec2(0.0f);
			v.materialIndex = 0;
			vertices.push_back(v);
		}
		indices = std::move(baked.mesh.indices);

		// One submesh covering the whole opaque part.
		SubmeshInfo sm{};
		sm.firstIndex   = 0;
		sm.indexCount   = static_cast<uint32_t>(indices.size());
		sm.firstVertex  = 0;
		sm.vertexCount  = static_cast<uint32_t>(vertices.size());
		sm.materialIndex = 0;
		sm.transparentMaterial = false;
		submeshes.push_back(sm);

		// LDraw carries no UVs, so tangents resolve to a harmless default (1,0,0,1);
		// call it anyway so no vertex is left with a zero-length tangent.
		calculateTangent();
		tangentsLoaded = true;

		std::cout << "[LDrawModelLoader] '" << parms.source << "': "
		          << vertices.size() << " verts, " << indices.size() / 3 << " tris, "
		          << connections_.size() << " connections ("
		          << males << " male / " << females << " female / "
		          << pins << " pin / " << axles << " axle)\n";
	}

} // namespace bagel
