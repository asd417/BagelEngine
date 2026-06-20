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
	//   ik:                        # two-bone IK chains, referenced by BONE NAME (skeleton joint
	//     - thigh: root            # node name). One list entry == one IKSetup.
	//       shin:  knee
	//       foot:  foot
	//       goal:  target          # bone the foot reaches for
	//       pole:  ik              # bone hinting the bend direction
	//       weight: 1.0            # optional (default 1.0)
	//       enabled: true          # optional (default true)
	//   attachments:               # named attach points ($attachment analog), bone-relative
	//     - name:   muzzle
	//       bone:   foot           # skeleton bone the point rides
	//       offset: [0, 1, 0]      # position in the bone's local space
	//       rotate: [0, 0, 0]      # optional Euler XYZ in DEGREES (default 0)
	struct ModelSidecar {
		// One IK chain as authored in the sidecar: bone NAMES (resolved to joint indices later,
		// once the skeleton's names are known — see ModelLoaderBase::resolveIkSetups).
		struct IkChain {
			std::string thigh, shin, foot, goal, pole;
			float weight  = 1.0f;
			bool  enabled = true;
		};

		// One attach point ($attachment analog): a name, the bone it rides (NAME, resolved later),
		// and a local offset (position + Euler rotation, radians) within that bone's space.
		struct Attachment {
			std::string name;
			std::string bone;
			glm::vec3   offset{ 0.0f };
			glm::vec3   rotate{ 0.0f }; // Euler XYZ, radians
		};

		// skins[skinIndex][slot] = override MaterialSource. Missing slot => model default.
		// skins.empty() => only the implicit default skin.
		std::vector<std::map<uint32_t, MaterialSource>> skins;
		std::vector<IkChain>    ikChains;    // authored IK chains (empty => none)
		std::vector<Attachment> attachments; // authored attach points (empty => none)

		uint32_t numSkins() const { return skins.empty() ? 1u : static_cast<uint32_t>(skins.size()); }

		// Look for the sibling ".yaml" of an engine-relative model source path
		// (e.g. "/models/x.gltf" -> "/models/x.yaml"). Returns empty (numSkins()==1) if the
		// file is absent or fails to parse.
		static ModelSidecar loadForModel(const std::string& modelSourcePath);
	};

} // namespace bagel
