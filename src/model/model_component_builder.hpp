#pragma once
#include "engine/bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "texture/bagel_textures.hpp"
#include "engine/bagel_descriptors.hpp"
#include "ecs/bagel_ecs_components.hpp"
#include "model/bagel_model.hpp"
#include "model/model_loaders/bagel_model_loader.hpp"
#include "animation/bagel_skin_manager.hpp"
#include "bagel_model_cache.hpp"
#include "bagel_util.hpp"
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



namespace bagel {
	//Component builder for generic model components
	class ModelComponentBuilder {
	public:
		ModelComponentBuilder(BGLDevice& bglDevice, entt::registry& registry);
		virtual ~ModelComponentBuilder() = default; // subclassed (e.g. LegoModelComponentBuilder)
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
			createVertexBuffer(sizeof(BGLModel::Vertex) * normalDataVertices.size(), (void*) normalDataVertices.data(), comp.vertexBuffer, comp.vertexMemory);

			comp.submeshes[comp.submeshCount++] = WireframeComponent::Submesh{};
			comp.vertexCount = static_cast<uint32_t>(normalDataVertices.size());
			
			normalDataVertices.clear();
			return comp;
		}
		void buildComponent(ModelComponent &mc, const char* modelFileName, const std::vector<glm::vec3> &verts, const std::vector<int> indices);
		
		
		// Resolve `modelFileName` to a shared, cache-owned Model (built once per source) and
		// attach a ModelComponent that references it. Deduplication is now just a cache lookup:
		// two entities using the same source point at ONE Model, and neither owns its buffers, so
		// destroying either entity frees nothing GPU-side (no more owner/borrower double-free).
		ModelComponent& buildComponent(entt::entity targetEnt, const char* modelFileName, ModelLoadSettings buildSettings)
		{
			ModelComponent& comp = registry.emplace<ModelComponent>(targetEnt);
			comp.loadSettings = buildSettings;
			comp.loadSettings.source = modelFileName; // source path = the model's identity / cache key

			ModelCacheManager& cache = ModelCacheManager::get();
			const std::string key = modelFileName;

			if (Model* cached = cache.find(key)) {
				// Cache hit: share the existing geometry. Non-skinned instances are fully
				// described by the shared Model, so there's nothing else to do.
				comp.model = cached;
				if (!cached->isSkinned) return comp;
				// Skinned duplicate (rare — skinned models are usually single-instance): the
				// influences/geometry are shared, but each entity needs its own AnimationComponent.
				// Re-parse the source for skeleton/clips (no buffers created) and bake playback state.
				loadModel(modelFileName, buildSettings);
				attachSkinningState(targetEnt);
				activeLoader.reset();
				return comp;
			}

			// Cache miss: build the shared Model once.
			Model& model = cache.create(key);
			model.loadSettings = comp.loadSettings;
			comp.model = &model;

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
				model.aabbMin = bMin;
				model.aabbMax = bMax;
			}

			std::cout << "Creating Vertex Buffer\n";
			// the assumption is that if the faces can be regenerated, the vertices will usually be regenerated as well
			model.mappedVB = createVertexBuffer(sizeof(BGLModel::Vertex) * vertices.size(), model.vertexBuffer, model.vertexMemory, buildSettings.isDeformable || buildSettings.isDynamic);
			if (indices.size() > 0) {
				std::cout << "Model has Index Buffer. Allocating...\n";
				model.mappedIB = createIndexBuffer(sizeof(uint32_t) * indices.size(), model.indexBuffer, model.indexMemory, buildSettings.isDynamic);
			}
			bool seenTransparent = false;
			for (auto& smi : submeshes) {
				assert(model.submeshCount < Model::MAX_SUBMESHES && "Exceeded MAX_SUBMESHES");
				// Loaders emit all solid submeshes before any transparent ones; track the
				// split so the model can filter by transparency with a single index.
				if (smi.transparentMaterial) {
					seenTransparent = true;
				} else {
					assert(!seenTransparent && "Submeshes must be ordered solid-first, then transparent");
					model.solidSubmeshCount++;
				}
				Model::Submesh& sm = model.submeshes[model.submeshCount++];
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
			model.indexCount  = static_cast<uint32_t>(indices.size());
			model.vertexCount = static_cast<uint32_t>(vertices.size());
			// Skin block the loader reserved/filled for this model (numSkins from the sidecar).
			model.skinBase = static_cast<uint16_t>(activeLoader->getSkinBase());
			model.numSlots = static_cast<uint16_t>(activeLoader->getNumSlots());
			model.numSkins = static_cast<uint8_t>(activeLoader->getNumSkins());

			// Skeletal skinning: upload per-vertex influences ONCE for the shared model, then bake
			// this entity's animation/playback state (a per-entity AnimationComponent).
			if (pSkinManager && activeLoader->isSkinned()) {
				auto& infl = activeLoader->getSkinInfluences();
				model.skinVertexBase = pSkinManager->uploadInfluences(infl.data(), static_cast<uint32_t>(infl.size()));
				model.isSkinned = true;
				attachSkinningState(targetEnt);
			}
			activeLoader.reset();
			std::cout << "Finished building Component\n";
			return comp;
		}

		// Bake a skinned entity's animation state from the currently-loaded activeLoader (skeleton +
		// clips): emplaces an AnimationComponent (playback + baked palette layout) and, if the
		// sidecar defines any, an AttachmentComponent. The model's shared skin offsets
		// (skinVertexBase/isSkinned) are set by the caller before this runs. Called on first build
		// and again for each (rare) skinned duplicate. Requires activeLoader loaded, pSkinManager set.
		void attachSkinningState(entt::entity targetEnt)
		{
			BakedAnimation baked = bakeClips(activeLoader->getSkeleton(), activeLoader->getAnimations());

			// Hot per-frame playback state (iterated every frame by updateAnimation + the animated
			// render passes) is split from the cold rig data (skeleton/pose/IK + per-clip tables).
			// They're separate EnTT pools, so holding both references across the two emplaces is safe.
			AnimationPlaybackComponent& play = registry.emplace<AnimationPlaybackComponent>(targetEnt);
			AnimationComponent&         anim = registry.emplace<AnimationComponent>(targetEnt);

			play.jointCount      = baked.jointCount;
			play.fps             = baked.fps;
			anim.fps             = baked.fps; // mirrored: clipDuration() (cold) needs it too
			anim.clipFrameBases  = baked.clipFrameBase;
			anim.clipFrameCounts = baked.clipFrameCount;
			// Seed the hot component's cached current-clip window (clip 0). animBaseOffset() reads
			// these scalars, not the tables — refresh them whenever `clip` changes (selectClip).
			if (!baked.clipFrameCount.empty()) {
				play.clipFrameBase  = baked.clipFrameBase[0];
				play.clipFrameCount = baked.clipFrameCount[0];
			}
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
				play.paletteBase = pSkinManager->uploadPalette(
					baked.matrices.data(), static_cast<uint32_t>(baked.matrices.size()));
			} else if (play.jointCount > 0) {
				const SkeletonData& skel = activeLoader->getSkeleton();
				std::vector<glm::mat4> restPalette(play.jointCount);
				resolvePalette(skel, skel.restPose, restPalette.data());
				play.paletteBase = pSkinManager->uploadPalette(restPalette.data(), play.jointCount);
				// Clip-less rig (e.g. the IK leg): present the single uploaded rest-pose palette as a
				// 1-frame clip and stop playback. Otherwise clipFrameCount stays 0, animBaseOffset()'s
				// frame clamp is skipped, and advancing `time` runs the palette index off the end of
				// the one uploaded frame — reading garbage matrices and collapsing the mesh to origin.
				play.clipFrameBase  = 0;
				play.clipFrameCount = 1;
				play.playing        = false;
			} else {
				play.paletteBase = 0;
			}

			// Manual posing: keep the skeleton at runtime, seed an editable pose from rest,
			// and reserve a dynamic palette region the engine overwrites when manualPose is on.
			anim.skeleton = activeLoader->getSkeleton();
			anim.editPose = anim.skeleton.restPose;
			if (play.jointCount > 0)
				play.dynamicPaletteBase = pSkinManager->reservePalette(play.jointCount);

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

			std::cout << "Skinned model: " << baked.jointCount << " joints, "
			          << anim.clipCount() << " clip(s)\n";
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

		// Loader factory for a file extension the base doesn't recognize. The base handles the
		// engine formats inline in loadModel() (.gltf/.glb/.obj) and returns nullptr here for
		// anything else (=> "unknown file type"). A derived builder overrides this to add formats
		// WITHOUT the engine knowing them — e.g. LegoModelComponentBuilder maps ".dat" to
		// LDrawModelLoader. `ext` includes the leading dot (".dat"), or is "" if the name has none.
		virtual std::unique_ptr<ModelLoaderBase> createLoaderForExtension(const char* ext) { (void)ext; return nullptr; }

		void* createVertexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst, bool useHostVisible = false);
		void* createVertexBuffer(size_t bufferSize, void* bufferSrc, VkBuffer& bufferDst, VkDeviceMemory& memoryDst, bool useHostVisible = false);
		void createVertexBufferDeviceLocal(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
		void* createVertexBufferHostVisible(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
		
		void* createIndexBuffer(size_t bufferSize, VkBuffer& bufferDst, VkDeviceMemory& memoryDst, bool useHostVisible = false);
		void createIndexBufferDeviceLocal(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
		void* createIndexBufferHostVisible(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
		// Allocate a host-mappable buffer in the BAR window (DEVICE_LOCAL|HOST_VISIBLE), falling
		// back to plain HOST_VISIBLE system memory (logged via CONSOLE) if that's unavailable.
		// `tag` names the buffer ("vertex"/"index") in the fallback log line.
		void createHostVisibleBuffer(size_t bufferSize, VkBufferUsageFlags usage, const char* tag, VkBuffer& bufferDst, VkDeviceMemory& memoryDst);
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