#pragma once
// Engine adapter: bakes an LDraw part via bagel::ldraw::Library and copies the
// result into the ModelLoaderBase arrays (vertices/indices/submeshes) so LEGO
// bricks flow through the exact same render pipeline as OBJ/glTF models.
//
// The baked connection points (studs / anti-stud tubes) are kept on the loader
// and exposed via getConnections() for the later snapping / Jolt-constraint work
// — the base pipeline itself only consumes the mesh.

#include "../model_loaders/bagel_model_loader.hpp"
#include "ldraw_library.hpp"

namespace bagel {

	class LDrawModelLoader : public ModelLoaderBase {
	public:
		// pTL is accepted for signature parity with the other loaders; LDraw geometry
		// carries no textures yet (color codes are a later concern), so it is unused.
		explicit LDrawModelLoader(BGLTextureLoader* pTL = nullptr) : ModelLoaderBase(pTL) {}
		~LDrawModelLoader() override = default;

		// parms.source is an LDraw part name ("3001", "3001.dat"). parms.scale /
		// parms.scaleVec scale the raw LDU geometry (1 LDU = 0.4 mm).
		void load(ModelLoadSettings parms) override;

		const std::vector<ldraw::ConnectionPoint>& getConnections() const { return connections_; }

	private:
		std::vector<ldraw::ConnectionPoint> connections_;
	};

} // namespace bagel
