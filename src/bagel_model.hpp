#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "bagel_textures.hpp"
#include "bagel_descriptors.hpp"
#include "bagel_ecs_components.hpp"
#include "model_loaders/bagel_model_loader.hpp"
#include "animation/bagel_skin_manager.hpp"

// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "tiny_gltf.h"
#include <memory>
#include <map>
#include <vector>
#include <tuple>
#include <ostream>
#include <functional>
#include <limits>

#include "bagel_util.hpp"

namespace bagel {
	struct GLTFMaterial {
		std::string name;
		uint16_t albedoMap = 0;
		uint16_t normalMap = 0;
		uint16_t metalRoughMap = 0;
		uint16_t specularMap = 0;
		uint16_t heightMap = 0;
		uint16_t opacityMap = 0;
		uint16_t aoMap = 0;
		uint16_t refractionMap = 0;
		uint16_t emissionMap = 0;
	};

	//Component builder for generic model components
	class ModelComponentBuilder {
	public:
		ModelComponentBuilder(BGLDevice& bglDevice, entt::registry& registry);
		void saveNormalData() { 
			assert(saveNextNormalData == false && "Already Set to save next normal data, retrieve existing data first");
			saveNextNormalData = true; 
		}

		//Using entt::entity and registry, this component builder will create components in the registry
		// As of 2024-09-25 Attemping to load GLTF and getting normal data can cause error because gltf loading does not save info to normalDataVertices
		// ComponentBuildMode::LINES is for wireframe rendering
		// ComponentBuildMode::FACES is for pbr rendering
		WireframeComponent& getNormalDataAsWireframe(entt::entity targetEnt) {
			assert(saveNextNormalData == true && "Save next normal data not set, can not retrieve normal data");
			saveNextNormalData = false;
			WireframeComponent& comp = registry.emplace<WireframeComponent>(targetEnt);
			createVertexBufferInputData(sizeof(BGLModel::Vertex) * normalDataVertices.size(), (void*) normalDataVertices.data(), comp.vertexBuffer, comp.vertexMemory);

			comp.submeshes[comp.submeshCount++] = ModelComponent::Submesh{};
			comp.vertexCount = static_cast<uint32_t>(normalDataVertices.size());
			
			normalDataVertices.clear();
			return comp;
		}

		template<typename T>
		T& buildComponent(entt::entity targetEnt, const char* modelFileName, ModelLoadSettings buildSettings)
		{
			//Check through registry if the model was already loaded by another entity
			entt::entity srcEnt = entt::null;
			for (auto [entity, existing] : registry.view<T>().each()) {
				if (existing.loadSettings.source == std::string(modelFileName)) { srcEnt = entity; break; }
			}
			if (srcEnt != entt::null) {
				//Already loaded — share the original's GPU buffers and submesh layout.
				T& newComp = registry.emplace<T>(targetEnt);
				// Re-fetch the source AFTER emplace: adding newComp can relocate the pool,
				// which would invalidate a reference taken during the scan above (and a
				// stale read of a moved-from ModelComponent would see nulled handles).
				T& comp = registry.get<T>(srcEnt);

				newComp.loadSettings = comp.loadSettings;
				// Borrow the original's shared Vk buffers; only the original frees them.
				newComp.ownsBuffers = false;

				newComp.submeshCount = comp.submeshCount;
				newComp.solidSubmeshCount = comp.solidSubmeshCount;
				for (uint32_t _i = 0; _i < comp.submeshCount; _i++)
					newComp.submeshes[_i] = comp.submeshes[_i];
				newComp.materialCount = comp.materialCount;
				for (uint32_t _i = 0; _i < comp.materialCount; _i++)
					newComp.materialSources[_i] = comp.materialSources[_i];
				// Deduped instances share the model's skin block; skinIndex stays per-entity (0).
				newComp.skinBase = comp.skinBase;
				newComp.numSlots = comp.numSlots;
				newComp.numSkins = comp.numSkins;
				// Skinning identity is shared too (same vertex influences + baked palette). The
				// AnimationComponent (playback state) is copied so the duplicate animates
				// independently while pointing at the same palette block.
				newComp.isSkinned      = comp.isSkinned;
				newComp.skinVertexBase = comp.skinVertexBase;
				if (comp.isSkinned && registry.all_of<AnimationComponent>(srcEnt))
					registry.emplace<AnimationComponent>(targetEnt, registry.get<AnimationComponent>(srcEnt));
				newComp.vertexBuffer = comp.vertexBuffer;
				newComp.indexBuffer  = comp.indexBuffer;
				newComp.vertexMemory = comp.vertexMemory;
				newComp.indexMemory  = comp.indexMemory;
				// indexCount/vertexCount drive the indexed-vs-nonindexed draw path —
				// must be copied or duplicates render the vertex buffer in raw order.
				newComp.indexCount   = comp.indexCount;
				newComp.vertexCount  = comp.vertexCount;
				newComp.aabbMin = comp.aabbMin;
				newComp.aabbMax = comp.aabbMax;
				newComp.frustumCull = comp.frustumCull;

				return newComp;
			}
			T& comp = registry.emplace<T>(targetEnt);
			// Record the authored recipe so the model can be rebuilt on map load.
			// loadModel resolves source internally on its own copy, so set the
			// caller-provided identifier here explicitly.
			comp.loadSettings = buildSettings;
			comp.loadSettings.source = modelFileName;
			loadModel(modelFileName, buildSettings);

			//Not supported in lines mode. There is no concept of face in lines mode and vertex array can be smaller than 3
			if(buildSettings.buildMode != LINES && !activeLoader->hasTangents()) activeLoader->calculateTangent();

			auto& vertices  = activeLoader->getVertices();
			auto& indices   = activeLoader->getIndices();
			auto& submeshes = activeLoader->getSubmeshes();

			// Compute model-space AABB
			{
				glm::vec3 bMin( std::numeric_limits<float>::max());
				glm::vec3 bMax(-std::numeric_limits<float>::max());
				for (const auto& v : vertices) {
					bMin = glm::min(bMin, v.position);
					bMax = glm::max(bMax, v.position);
				}
				comp.aabbMin = bMin;
				comp.aabbMax = bMax;
			}

			std::cout << "Creating Vertex Buffer\n";
			createVertexBuffer(sizeof(BGLModel::Vertex) * vertices.size(), comp.vertexBuffer, comp.vertexMemory);
			if (indices.size() > 0) {
				std::cout << "Model has Index Buffer. Allocating...\n";
				createIndexBuffer(sizeof(uint32_t) * indices.size(), comp.indexBuffer, comp.indexMemory);
			}
			bool seenTransparent = false;
			for (auto& smi : submeshes) {
				assert(comp.submeshCount < ModelComponent::MAX_SUBMESHES && "Exceeded MAX_SUBMESHES");
				// Loaders emit all solid submeshes before any transparent ones; track the
				// split so ModelComponent can filter by transparency with a single index.
				if (smi.transparentMaterial) {
					seenTransparent = true;
				} else {
					assert(!seenTransparent && "Submeshes must be ordered solid-first, then transparent");
					comp.solidSubmeshCount++;
				}
				ModelComponent::Submesh& sm = comp.submeshes[comp.submeshCount++];
				sm.firstIndex    = smi.firstIndex;
				sm.indexCount    = smi.indexCount;
				sm.firstVertex   = smi.firstVertex;
				sm.vertexCount   = smi.vertexCount;
				sm.materialIndex = smi.materialIndex;
				// Per-submesh AABB in model space
				glm::vec3 sMin( std::numeric_limits<float>::max());
				glm::vec3 sMax(-std::numeric_limits<float>::max());
				uint32_t vEnd = smi.firstVertex + smi.vertexCount;
				for (uint32_t vi = smi.firstVertex; vi < vEnd; vi++) {
					sMin = glm::min(sMin, vertices[vi].position);
					sMax = glm::max(sMax, vertices[vi].position);
				}
				sm.aabbMin = sMin;
				sm.aabbMax = sMax;
			}
			comp.indexCount  = static_cast<uint32_t>(indices.size());
			comp.vertexCount = static_cast<uint32_t>(vertices.size());
			// Skin block the loader reserved/filled for this model (numSkins from the sidecar).
			comp.skinBase = static_cast<uint16_t>(activeLoader->getSkinBase());
			comp.numSlots = static_cast<uint16_t>(activeLoader->getNumSlots());
			comp.numSkins = static_cast<uint8_t>(activeLoader->getNumSkins());

			// Skeletal skinning: if the model carries influences and a skin manager is set,
			// upload per-vertex influences + the baked joint palette, and attach playback state.
			if (pSkinManager && activeLoader->isSkinned()) {
				auto& infl = activeLoader->getSkinInfluences();
				comp.skinVertexBase = pSkinManager->uploadInfluences(infl.data(), static_cast<uint32_t>(infl.size()));
				comp.isSkinned = true;

				BakedAnimation baked = bakeClips(activeLoader->getSkeleton(), activeLoader->getAnimations());
				AnimationComponent& anim = registry.emplace<AnimationComponent>(targetEnt);
				anim.jointCount     = baked.jointCount;
				anim.fps            = baked.fps;
				anim.clipFrameBase  = baked.clipFrameBase;
				anim.clipFrameCount = baked.clipFrameCount;
				// Carry the glTF animation names alongside the baked frame table (same clip order).
				{
					const auto& clips = activeLoader->getAnimations();
					anim.clipNames.reserve(clips.size());
					for (const auto& c : clips) anim.clipNames.push_back(c.name);
				}
				// With clips, paletteBase points at the baked resident region. With NO clips, bake a
				// single bind/rest-pose palette and point paletteBase at it — otherwise animBaseOffset()
				// returns paletteBase=0, an unwritten palette region, and every vertex collapses to the
				// origin (the model renders invisible). A clip-less skinned model should show its rest pose.
				if (!baked.matrices.empty()) {
					anim.paletteBase = pSkinManager->uploadPalette(
						baked.matrices.data(), static_cast<uint32_t>(baked.matrices.size()));
				} else if (anim.jointCount > 0) {
					const SkeletonData& skel = activeLoader->getSkeleton();
					std::vector<glm::mat4> restPalette(anim.jointCount);
					resolvePalette(skel, skel.restPose, restPalette.data());
					anim.paletteBase = pSkinManager->uploadPalette(restPalette.data(), anim.jointCount);
				} else {
					anim.paletteBase = 0;
				}

				// Manual posing: keep the skeleton at runtime, seed an editable pose from rest,
				// and reserve a dynamic palette region the engine overwrites when manualPose is on.
				anim.skeleton = activeLoader->getSkeleton();
				anim.editPose = anim.skeleton.restPose;
				if (anim.jointCount > 0)
					anim.dynamicPaletteBase = pSkinManager->reservePalette(anim.jointCount);

				// IK chains come from the "<model>.yaml" sidecar (bone names resolved to joint
				// indices now that the skeleton is parsed). These are NOT serialized with the map —
				// the sidecar is their single source of truth — so they're (re)attached on every
				// load/rehydrate. Only the authored pose (editPose) persists in the map.
				anim.ikSetups = activeLoader->resolveIkSetups();

				// Attach points from the same sidecar (also transient / sidecar-owned). Only added
				// when the model defines any, so unattached models stay AttachmentComponent-free.
				auto attachPoints = activeLoader->resolveAttachments();
				if (!attachPoints.empty()) {
					auto& ac = registry.emplace<AttachmentComponent>(targetEnt);
					ac.points = std::move(attachPoints);
				}

				std::cout << "Skinned model: " << infl.size() << " influences, "
				          << baked.jointCount << " joints, " << anim.clipCount() << " clip(s)\n";
			}
			activeLoader.reset();
			std::cout << "Finished building Component\n";
			return comp;
		}

		void configureModelMaterialSet(std::vector<GLTFMaterial>* set);

		void removeModelMaterialSet() { p_materialSet = nullptr; }

		// Optional: set before buildComponent() to enable texture loading for OBJ/GLTF models
		void setTextureLoader(BGLTextureLoader* tl) { pTextureLoader = tl; }
		// Optional: set before buildComponent() so loaders register their materials into the
		// global material table (and vertices store global material indices).
		void setMaterialManager(BGLMaterialManager* mm) { pMaterialManager = mm; }
		// Optional: set before buildComponent() to enable skeletal skinning. When a loaded model
		// carries skin influences, the builder uploads them + the baked palette here and attaches
		// an AnimationComponent. Without it, skinned models load as static (no skinning).
		void setSkinManager(BGLSkinManager* sm) { pSkinManager = sm; }
	protected:
		void loadModel(const char* filename, ModelLoadSettings buildSettings);

		//Creates vertex buffer with any given set of BGLModel::Vertex
		void createVertexBufferInputData(size_t bufferSize, void* bufferSrc, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
		//Creates vertex buffer with generated vertices
		void createVertexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
		void createIndexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);

		bool saveNextNormalData = false;

		std::unique_ptr<ModelLoaderBase> activeLoader;
		std::vector<BGLModel::Vertex> normalDataVertices{};
		std::vector<GLTFMaterial>* p_materialSet = nullptr;
		BGLTextureLoader* pTextureLoader = nullptr;
		BGLMaterialManager* pMaterialManager = nullptr;
		BGLSkinManager* pSkinManager = nullptr;

		BGLDevice& bglDevice;
		entt::registry& registry;
	};
}