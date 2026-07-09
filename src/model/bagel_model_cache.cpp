#include "bagel_model_cache.hpp"

namespace bagel {

	ModelCacheManager& ModelCacheManager::get() {
		static ModelCacheManager instance;
		return instance;
	}

	Model* ModelCacheManager::find(const std::string& key) {
		auto it = models_.find(key);
		return it == models_.end() ? nullptr : it->second.get();
	}

	Model& ModelCacheManager::create(const std::string& key) {
		auto [it, inserted] = models_.emplace(key, std::make_unique<Model>());
		assert(inserted && "ModelCacheManager::create called for an already-cached key");
		(void)inserted;
		return *it->second;
	}

	void ModelCacheManager::clear() {
		models_.clear();
	}

} // namespace bagel
