#pragma once
// LEGO-aware ModelComponentBuilder. The engine's ModelComponentBuilder knows only its own
// formats (.gltf/.glb/.obj + procedural names); it delegates any other extension to
// createLoaderForExtension(). This subclass supplies the ".dat" -> LDrawModelLoader mapping so
// LEGO parts flow through the exact same build path without the engine depending on lego/.
//
// Use this instead of ModelComponentBuilder anywhere a ".dat" part may be built: PartSystem
// spawning, and map rehydrate when a saved scene contains LEGO parts.

#include "../bagel_model.hpp"        // ModelComponentBuilder
#include "ldraw_model_loader.hpp"    // LDrawModelLoader

#include <cstring>
#include <memory>

namespace bagel {

	class LegoModelComponentBuilder : public ModelComponentBuilder {
	public:
		using ModelComponentBuilder::ModelComponentBuilder; // inherit (BGLDevice&, entt::registry&)

	protected:
		std::unique_ptr<ModelLoaderBase> createLoaderForExtension(const char* ext) override
		{
			if (ext && std::strcmp(ext, ".dat") == 0)
				return std::make_unique<LDrawModelLoader>(pTextureLoader);
			return ModelComponentBuilder::createLoaderForExtension(ext); // -> nullptr (unknown)
		}
	};

} // namespace bagel
