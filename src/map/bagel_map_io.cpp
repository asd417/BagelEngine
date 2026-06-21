#include "bagel_map_io.hpp"

#include "../bagel_model.hpp"     // ModelComponentBuilder (+ components, MaterialSource, Pose)
#include "../bagel_material.hpp"  // BGLMaterialManager
// BGLSkinManager (animation/bagel_skin_manager.hpp) and BGLJolt (physics/bagel_jolt.hpp via
// the header) come in transitively through the includes above / bagel_map_io.hpp.

#include <type_traits>
#include <vector>

namespace bagel {

	// ---- rehydrate helper ---------------------------------------------------
	// Rebuild the GPU geometry (and generated-model materials) for every entity that
	// carries model component T. LoadRegistry restored only loadSettings/frustumCull/
	// materialSources; the VkBuffers + submeshes must be re-cooked from the recipe.
	//
	// Ordering matters: we COLLECT every recipe, REMOVE T from all of them, and only
	// THEN rebuild. buildComponent dedups by scanning live components for a matching
	// loadSettings.source and borrows the original's VkBuffers — if we rebuilt in place
	// it could match a not-yet-rebuilt component that has only VK_NULL_HANDLE buffers.
	template<class T>
	static void rehydrateModelType(entt::registry& registry, ModelComponentBuilder& builder)
	{
		struct Recipe {
			entt::entity e;
			ModelLoadSettings ls;
			bool frustumCull;
			uint8_t skinIndex;
			uint32_t materialCount;
			std::vector<MaterialSource> sources; // only the [0, materialCount) valid slots
			// Authored pose carried across the rebuild (skinned ModelComponents only). buildComponent
			// re-emplaces a FRESH AnimationComponent (editPose = rest pose), so we stash the restored
			// pose here and re-apply it afterward. IK is NOT carried — it comes from the sidecar and
			// buildComponent re-attaches it, so we must not clobber that with stale map data.
			bool hasAnim = false;
			bool animManual = false;
			Pose animEditPose;
		};
		std::vector<Recipe> recipes;
		for (auto [e, m] : registry.view<T>().each()) {
			Recipe r{ e, m.loadSettings, m.frustumCull, m.skinIndex, m.materialCount,
			          { m.materialSources, m.materialSources + m.materialCount } };
			if constexpr (std::is_same_v<T, ModelComponent>) {
				if (auto* a = registry.try_get<AnimationComponent>(e)) {
					r.hasAnim = true;
					r.animManual   = a->manualPose;
					r.animEditPose = a->editPose;
				}
			}
			recipes.push_back(std::move(r));
		}

		for (auto& r : recipes) {
			registry.remove<T>(r.e);
			// Drop the restored AnimationComponent too, so buildComponent's emplace doesn't collide;
			// its authored fields are saved on the recipe and re-applied below. AttachmentComponent
			// is transient (sidecar-rebuilt) — remove() is a no-op if absent, kept for safety.
			if constexpr (std::is_same_v<T, ModelComponent>) {
				if (r.hasAnim) registry.remove<AnimationComponent>(r.e);
				registry.remove<AttachmentComponent>(r.e);
			}
		}

		for (auto& r : recipes) {
			T& m = builder.buildComponent<T>(r.e, r.ls.source.c_str(), r.ls);
			m.frustumCull = r.frustumCull;
			m.setSkin(r.skinIndex); // re-apply the saved skin (numSkins came back from the sidecar)
			// Restore the generated-material source paths (OBJ/GLTF have materialCount == 0;
			// their materials are re-baked into the vertex buffer by buildComponent above).
			for (uint32_t i = 0; i < r.sources.size() && i < ModelComponent::MAX_MATERIALS; ++i)
				m.setMaterialSource(i, r.sources[i]);

			// Re-apply the authored pose onto the freshly built AnimationComponent. editPose is only
			// restored when its length matches the rebuilt skeleton's joint count (a changed asset
			// could differ). IK setups are left as buildComponent attached them (from the sidecar).
			if constexpr (std::is_same_v<T, ModelComponent>) {
				if (r.hasAnim) {
					if (auto* a = registry.try_get<AnimationComponent>(r.e)) {
						a->manualPose = r.animManual;
						if (r.animEditPose.size() == a->editPose.size())
							a->editPose = std::move(r.animEditPose);
						a->poseDirty = true; // force a palette re-resolve from the restored pose
					}
				}
			}
		}
	}

	void Map::rehydrate(entt::registry& registry, BGLDevice& bglDevice,
	                    BGLMaterialManager& materialManager, BGLSkinManager& skinManager)
	{
		ModelComponentBuilder builder(bglDevice, registry);
		builder.setTextureLoader(&materialManager.getTextureLoader());
		builder.setMaterialManager(&materialManager);
		builder.setSkinManager(&skinManager);
		rehydrateModelType<ModelComponent>(registry, builder);
		rehydrateModelType<WireframeComponent>(registry, builder);
		rehydrateModelType<CollisionModelComponent>(registry, builder);

		// Rebuild live Jolt bodies from the restored BodyCreationSettings; this reissues
		// the transient BodyIDs (the loaded ones are meaningless).
		BGLJolt::GetInstance()->RehydratePhysicsBodies();
	}

} // namespace bagel
