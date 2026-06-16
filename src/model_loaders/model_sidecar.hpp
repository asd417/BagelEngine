#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "components/model.hpp" // MaterialSource

namespace bagel {

	// Parsed "<model>.yaml" sidecar describing in-game properties of a model. First property:
	// model skins (Source's $texturegroup / skin families).
	//
	// YAML schema:
	//   materials:                 # named MaterialSource definitions (engine-relative paths)
	//     body_red: { albedo: "/materials/body_red.png", normal: "/materials/body_n.png" }
	//     head_red: { albedo: "/materials/head_red.png" }
	//   skins:                     # rows = skins (skin 0 = default); slot index -> material name
	//     - {}                     # skin 0: all model defaults
	//     - { 0: body_red, 4: head_red }   # skin 1: override slots 0 and 4
	//     - { 0: body_blue }                # skin 2
	// A slot a skin doesn't list keeps the model's own (glTF/OBJ) material.
	struct ModelSidecar {
		// skins[skinIndex][slot] = override MaterialSource. Missing slot => model default.
		// skins.empty() => only the implicit default skin.
		std::vector<std::map<uint32_t, MaterialSource>> skins;

		uint32_t numSkins() const { return skins.empty() ? 1u : static_cast<uint32_t>(skins.size()); }

		// Look for the sibling ".yaml" of an engine-relative model source path
		// (e.g. "/models/x.gltf" -> "/models/x.yaml"). Returns empty (numSkins()==1) if the
		// file is absent or fails to parse.
		static ModelSidecar loadForModel(const std::string& modelSourcePath);
	};

} // namespace bagel
