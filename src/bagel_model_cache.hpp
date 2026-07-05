#pragma once
// Engine-level cache that OWNS every loaded Model's GPU geometry, keyed by source path.
// One Model per source is shared (flyweight) by all entities that use it; entities hold a
// Model* (via ModelComponent) and never free it. This is what makes entity deletion safe:
// destroying an entity drops a reference but frees no GPU buffer, so it can never dangle a
// buffer another instance is still drawing.
//
// Lifetime: buffers live until clear(). The owner (Application) must call clear() on scene
// unload and again at shutdown BEFORE the VkDevice is destroyed, with the GPU idle — a
// Model's destructor calls vkDestroyBuffer/vkFreeMemory.

#include "components/model.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <unordered_map>

namespace bagel {

	class ModelCacheManager {
	public:
		// Process-wide instance. Meyers singleton: its static-storage map is emptied by clear()
		// at shutdown, so the exit-time destructor sees no live buffers (no post-device frees).
		static ModelCacheManager& get();

		// O(1) lookup by source path. nullptr if this source has not been built yet.
		Model* find(const std::string& key);

		// Insert a fresh, empty Model for `key` (the caller populates its buffers/submeshes) and
		// return it. Asserts `key` is not already cached — callers find() first.
		Model& create(const std::string& key);

		// Destroy every Model (frees their GPU buffers). Call at scene unload and before device
		// teardown, with the GPU idle. Entities referencing these Models must be gone or unused.
		void clear();

		size_t size() const { return models_.size(); }

	private:
		ModelCacheManager() = default;
		ModelCacheManager(const ModelCacheManager&) = delete;
		ModelCacheManager& operator=(const ModelCacheManager&) = delete;

		// unique_ptr keeps each Model's address stable across rehash, so the Model* handed to
		// ModelComponent stays valid as the cache grows.
		std::unordered_map<std::string, std::unique_ptr<Model>> models_;
	};

} // namespace bagel
