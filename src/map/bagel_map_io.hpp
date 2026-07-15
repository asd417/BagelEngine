#pragma once

// In an ECS game engine, a map can be defined as a set of predefined entities and its component properties.
// for this, components need to know what data it has can be serialized.
// we need a way to serialize and deserialize all components, and an easy way to expand this.
//
// The per-component serialization (the `serialize` overloads, the binary archives,
// and SaveRegistry/LoadRegistry) lives in bagel_ecs_serialize.hpp. A *map* is just
// that registry snapshot written to / read from a file, wrapped in a small,
// versioned file header so we can detect bad/old files instead of reading garbage.
//
// File layout:
//   [ 4 bytes  ] magic  "BMAP"
//   [ uint32   ] version
//   [ ...bytes ] registry snapshot (SaveRegistry / LoadRegistry payload)
//
// Expanding the map = adding a component to the SaveRegistry/LoadRegistry manifest
// (and its serialize overload). When the payload format changes incompatibly, bump
// VERSION so old files are rejected.

#include "ecs/bagel_ecs_serialize.hpp"
#include "engine/bagel_engine_device.hpp"
#include "physics/bagel_jolt.hpp"

#include "entt.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace bagel {

	// Forward-declared (used by rehydrate() below as references only). The full definitions
	// pull in heavy model/material headers, so they live in bagel_map_io.cpp — this header is
	// included widely (via components/tag.hpp) and must stay light to avoid a circular include.
	class BGLMaterialManager;    // bagel_material.hpp
	class BGLSkinManager;        // animation/bagel_skin_manager.hpp
	class ModelComponentBuilder; // bagel_model.hpp — passed in so the app can supply a format-aware
	                             // subclass (e.g. LegoModelComponentBuilder) without map IO knowing it

	struct Map {
		static constexpr char     MAGIC[4] = { 'B', 'M', 'A', 'P' };
		static constexpr std::uint32_t VERSION = 6; // v6: PlanetComponent paint cube-map removed (TerrainConfig only)

		// True if a map file exists at `path` (used to "load only if it exists").
		static bool exists(const std::string& path) {
			std::error_code ec;
			return std::filesystem::exists(path, ec);
		}

		// Unload the active scene. Tears down the subsystem-owned transient state that the
		// component destructors can't reach, in the right order, then clears the registry.
		// Call this — not registry.clear() directly — whenever the current scene is dropped
		// (map load, scene switch, shutdown) so nothing leaks.
		static void unload(entt::registry& registry) {
			// GPU may still be reading this scene's vertex/index buffers; wait before the
			// ModelComponent destructors (run by clear()) free them.
			vkDeviceWaitIdle(BGLDevice::device());
			// Jolt bodies aren't owned by the components (no destructor touches the physics
			// system), so remove them explicitly before the components vanish.
			BGLJolt::GetInstance()->RemoveAllBodies();
			// Component destructors cascade here: GPU buffers (ModelComponent /
			// DataBufferComponent) are freed as their entities are destroyed.
			registry.clear();
		}

		// Write every entity + serializable component in `registry` to `path`.
		// Creates the parent directory if needed. Returns false if the file can't be
		// opened or the stream errors out.
		static bool save(const entt::registry& registry, const std::string& path) {
			std::error_code ec;
			const std::filesystem::path parent = std::filesystem::path(path).parent_path();
			if (!parent.empty()) std::filesystem::create_directories(parent, ec);

			std::ofstream os(path, std::ios::binary | std::ios::trunc);
			if (!os) return false;

			os.write(MAGIC, sizeof(MAGIC));
			const std::uint32_t version = VERSION;
			os.write(reinterpret_cast<const char*>(&version), sizeof(version));

			SaveRegistry(registry, os);
			return static_cast<bool>(os);
		}

		// Load a map file at `path` into `registry`. The current scene is unloaded first
		// (unload() clears the registry — snapshot_loader restores entity identifiers and
		// expects to own them). Returns false on open error, bad magic, or unknown version;
		// in those cases the current scene is left untouched (unload happens only once the
		// header validates).
		//
		// NOTE: this restores PERSISTENT state only. Transient state (GPU buffers,
		// physics bodies, bindless handles) is left at defaults — run your rehydrate
		// pass afterwards to rebuild it (see bagel_ecs_serialize.hpp).
		static bool load(entt::registry& registry, const std::string& path) {
			std::ifstream is(path, std::ios::binary);
			if (!is) return false;

			char magic[4] = {};
			is.read(magic, sizeof(magic));
			if (!is || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0) return false;

			std::uint32_t version = 0;
			is.read(reinterpret_cast<char*>(&version), sizeof(version));
			if (!is || version != VERSION) return false;

			unload(registry);
			LoadRegistry(registry, is);
			return static_cast<bool>(is) || is.eof();
		}

		// Rebuild the TRANSIENT state that load() leaves at defaults: re-cook every model
		// component's GPU buffers + materials from its serialized recipe, and reissue Jolt
		// bodies from their restored creation settings. Call once after a successful load().
		// Defined in bagel_map_io.cpp (needs ModelComponentBuilder / the material+skin managers).
		// `builder` is supplied by the caller so a saved scene containing app-specific formats
		// (e.g. LEGO ".dat" parts) rebuilds through a matching subclass — map IO stays format-agnostic.
		static void rehydrate(entt::registry& registry, ModelComponentBuilder& builder,
		                      BGLMaterialManager& materialManager, BGLSkinManager& skinManager);
	};

} // namespace bagel
