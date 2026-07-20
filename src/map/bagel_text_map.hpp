#pragma once

// Human-readable text maps — a "basic static map" authored as a YAML file, inspired by
// Source's entity model. A map file has two sections:
//
//   prefabs:   entity CLASSES (like Source FGD): each is a list of components, with
//              $placeholder fields to be filled per instance.
//   entities:  the INSTANCES (like a VMF): each picks a class and supplies the values
//              its prefab references via $vars.
//
// Example:
//   prefabs:
//     prop_static:
//       - { type: TransformComponent, position: $position, rotation: $rotation, scale: $scale }
//       - { type: ModelComponent, source: $model, mergesolidsubmeshes: $merge }
//   entities:
//     - { class: prop_static, model: /models/sponza/Sponza.gltf, position: [0,0,0], scale: [0.01,0.01,0.01] }
//
// This is a one-way authoring format: it BUILDS a live scene (registry + GPU state). It is
// not the binary save format — that's Map (bagel_map_io.hpp), which round-trips a running
// scene. Text maps are for hand-writing; save one as a .bmap if you want to persist edits.
//
// Coordinates are engine world space (Y is down). Rotations are in DEGREES.

#include "entt.hpp"

#include <glm/glm.hpp>

#include <string>

namespace bagel {

	// Forward-declared: the loader routes ModelComponents through the builder (which needs the
	// material/skin managers) but map IO stays free of the heavy model headers. Full types are
	// pulled in by bagel_text_map.cpp.
	class ModelComponentBuilder;
	class BGLMaterialManager;
	class BGLSkinManager;

	struct TextMap {
		// A map may declare its own initial camera pose via an info_player_start entity. It has
		// no ECS representation (the camera is the app's free-fly viewer), so it's reported back
		// to the caller instead of emplaced. `set` stays false when the map has no spawn.
		struct SpawnPoint {
			bool      set = false;
			glm::vec3 position{0.0f};
			glm::vec3 rotation{0.0f}; // degrees
		};

		// True if a text-map file exists at `path`.
		static bool exists(const std::string& path);

		// Parse the YAML text map at `path` (an already-resolved filesystem path) and build its
		// entities into `registry`. Static props are cooked through `builder`, so their GPU
		// buffers/materials are ready to render on return; `materialManager`/`skinManager` are
		// used to configure that builder the same way Map::rehydrate does.
		//
		// Does NOT clear the registry first — call Map::unload() before this if replacing the
		// current scene. Returns false if the file can't be opened or the YAML fails to parse
		// (individual bad entities are skipped and logged, not fatal). If the map contains an
		// info_player_start, its pose is written to `outSpawn`.
		static bool load(entt::registry& registry, const std::string& path,
		                 ModelComponentBuilder& builder,
		                 BGLMaterialManager& materialManager, BGLSkinManager& skinManager,
		                 SpawnPoint& outSpawn);
	};

} // namespace bagel
