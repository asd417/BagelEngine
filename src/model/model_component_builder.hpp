#pragma once
#include "animation/bagel_skin_manager.hpp"
#include "bagel_buffer.hpp"
#include "bagel_model_cache.hpp"
#include "bagel_util.hpp"
#include "ecs/bagel_ecs_components.hpp"
#include "engine/bagel_descriptors.hpp"
#include "engine/bagel_engine_device.hpp"
#include "model/bagel_model.hpp"
#include "model/model_loaders/bagel_model_loader.hpp"
#include "texture/bagel_textures.hpp"

// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "tiny_gltf.h"
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <tuple>
#include <vector>

namespace bagel
{
// Component builder for generic model components
class ModelComponentBuilder
{
  public:
    ModelComponentBuilder(BGLDevice &bglDevice, entt::registry &registry);
    virtual ~ModelComponentBuilder() = default; // subclassed (e.g. LegoModelComponentBuilder)
    void saveNormalData()
    {
        assert(saveNextNormalData == false && "Already Set to save next normal data, retrieve existing data first");
        saveNextNormalData = true;
    }

    // Using entt::entity and registry, this component builder will create components in the registry
    //  As of 2024-09-25 Attemping to load GLTF and getting normal data can cause error because gltf loading does not save info to normalDataVertices
    //  ComponentBuildMode::LINES is for wireframe rendering
    //  ComponentBuildMode::FACES is for pbr rendering
    WireframeComponent &getNormalDataAsWireframe(entt::entity targetEnt)
    {
        assert(saveNextNormalData == true && "Save next normal data not set, can not retrieve normal data");
        saveNextNormalData = false;
        WireframeComponent &comp = registry.emplace<WireframeComponent>(targetEnt);
        createVertexBuffer(sizeof(BGLModel::Vertex) * normalDataVertices.size(), (void *)normalDataVertices.data(), comp.vertexBuffer, comp.vertexMemory);

        comp.submeshes[comp.submeshCount++] = WireframeComponent::Submesh{};
        comp.vertexCount = static_cast<uint32_t>(normalDataVertices.size());

        normalDataVertices.clear();
        return comp;
    }

    void buildComponent(ModelComponent &mc, const char *modelFileName, const std::vector<BGLModel::Vertex> &verts, const std::vector<uint32_t> &indices, const std::vector<SubmeshInfo> &submeshes, bool mapped = false);
    // if the new vertex and index buffer has the size less or equal to the original, you can edit it in place.
    void editComponent(ModelComponent &mc, const char *modelFileName, const std::vector<BGLModel::Vertex> &verts, const std::vector<uint32_t> &indices, const std::vector<SubmeshInfo> &submeshes);

    // Resolve `modelFileName` to a shared, cache-owned Model (built once per source) and
    // attach a ModelComponent that references it. Deduplication is now just a cache lookup:
    // two entities using the same source point at ONE Model, and neither owns its buffers, so
    // destroying either entity frees nothing GPU-side (no more owner/borrower double-free).
    ModelComponent &buildComponent(entt::entity targetEnt, const char *modelFileName, ModelLoadSettings buildSettings);

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
        AnimationPlaybackComponent &play = registry.emplace<AnimationPlaybackComponent>(targetEnt);
        AnimationComponent &anim = registry.emplace<AnimationComponent>(targetEnt);

        play.jointCount = baked.jointCount;
        play.fps = baked.fps;
        anim.fps = baked.fps; // mirrored: clipDuration() (cold) needs it too
        anim.clipFrameBases = baked.clipFrameBase;
        anim.clipFrameCounts = baked.clipFrameCount;
        // Seed the hot component's cached current-clip window (clip 0). animBaseOffset() reads
        // these scalars, not the tables — refresh them whenever `clip` changes (selectClip).
        if (!baked.clipFrameCount.empty())
        {
            play.clipFrameBase = baked.clipFrameBase[0];
            play.clipFrameCount = baked.clipFrameCount[0];
        }
        // Carry the glTF animation names alongside the baked frame table (same clip order).
        {
            const auto &clips = activeLoader->getAnimations();
            anim.clipNames.reserve(clips.size());
            for (const auto &c : clips)
                anim.clipNames.push_back(c.name);
        }
        // With clips, paletteBase points at the baked resident region. With NO clips, bake a
        // single bind/rest-pose palette and point paletteBase at it — otherwise animBaseOffset()
        // returns paletteBase=0, an unwritten palette region, and every vertex collapses to the
        // origin (the model renders invisible). A clip-less skinned model should show its rest pose.
        if (!baked.matrices.empty())
        {
            play.paletteBase = pSkinManager->uploadPalette(
                baked.matrices.data(), static_cast<uint32_t>(baked.matrices.size()));
        }
        else if (play.jointCount > 0)
        {
            const SkeletonData &skel = activeLoader->getSkeleton();
            std::vector<glm::mat4> restPalette(play.jointCount);
            resolvePalette(skel, skel.restPose, restPalette.data());
            play.paletteBase = pSkinManager->uploadPalette(restPalette.data(), play.jointCount);
            // Clip-less rig (e.g. the IK leg): present the single uploaded rest-pose palette as a
            // 1-frame clip and stop playback. Otherwise clipFrameCount stays 0, animBaseOffset()'s
            // frame clamp is skipped, and advancing `time` runs the palette index off the end of
            // the one uploaded frame — reading garbage matrices and collapsing the mesh to origin.
            play.clipFrameBase = 0;
            play.clipFrameCount = 1;
            play.playing = false;
        }
        else
        {
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
        if (!attachPoints.empty())
        {
            auto &ac = registry.emplace<AttachmentComponent>(targetEnt);
            ac.points = std::move(attachPoints);
        }

        std::cout << "Skinned model: " << baked.jointCount << " joints, "
                  << anim.clipCount() << " clip(s)\n";
    }

    void configureModelMaterialSet(std::vector<GLTFMaterial> *set);

    void removeModelMaterialSet()
    {
        p_materialSet = nullptr;
    }

    // Optional: set before buildComponent() to enable texture loading for OBJ/GLTF models
    void setTextureLoader(BGLTextureLoader *tl)
    {
        pTextureLoader = tl;
    }
    // Optional: set before buildComponent() so loaders register their materials into the
    // global material table (and vertices store global material indices).
    void setMaterialManager(BGLMaterialManager *mm)
    {
        pMaterialManager = mm;
    }
    // Optional: set before buildComponent() to enable skeletal skinning. When a loaded model
    // carries skin influences, the builder uploads them + the baked palette here and attaches
    // an AnimationComponent. Without it, skinned models load as static (no skinning).
    void setSkinManager(BGLSkinManager *sm)
    {
        pSkinManager = sm;
    }

  protected:
    void loadModel(const char *filename, ModelLoadSettings buildSettings);

    // Shared geometry-metadata helpers used by every build/edit path.
    // computeModelBounds: overall model-space AABB from the vertices.
    // populateSubmeshes: (re)fill the model's submesh table + per-submesh AABBs. Resets the
    // submesh/solid counts first, so it serves both initial builds and in-place edits (where
    // the counts may shrink).
    static void computeModelBounds(Model &model, const std::vector<BGLModel::Vertex> &verts);
    static void populateSubmeshes(Model &model, const std::vector<SubmeshInfo> &submeshes, const std::vector<BGLModel::Vertex> &verts);

    // Loader factory for a file extension the base doesn't recognize. The base handles the
    // engine formats inline in loadModel() (.gltf/.glb/.obj) and returns nullptr here for
    // anything else (=> "unknown file type"). A derived builder overrides this to add formats
    // WITHOUT the engine knowing them — e.g. LegoModelComponentBuilder maps ".dat" to
    // LDrawModelLoader. `ext` includes the leading dot (".dat"), or is "" if the name has none.
    virtual std::unique_ptr<ModelLoaderBase> createLoaderForExtension(const char *ext)
    {
        (void)ext;
        return nullptr;
    }

    // `mapped` selects the upload path: false = staged (device-local, immutable after upload),
    // true = persistently mapped (CPU-writable; prefers the BAR window, see createMappableBuffer).
    void *createVertexBuffer(size_t bufferSize, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped = false);
    void *createVertexBuffer(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped = false);
    // Staged: upload once via a staging buffer into device-local memory; not host-mappable afterward.
    void createVertexBufferStaged(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
    // Mapped: allocate host-visible memory and return the persistent mapped pointer for in-place updates.
    void *createVertexBufferMapped(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);

    void *createIndexBuffer(size_t bufferSize, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped = false);
    void *createIndexBuffer(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped = false);
    void createIndexBufferStaged(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
    void *createIndexBufferMapped(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
    // Allocate a host-mappable buffer, preferring the BAR window (DEVICE_LOCAL|HOST_VISIBLE) —
    // CPU-writable VRAM — and falling back to plain HOST_VISIBLE system memory (logged via CONSOLE)
    // if that's unavailable. `tag` names the buffer ("vertex"/"index") in the fallback log line.
    void createMappableBuffer(size_t bufferSize, VkBufferUsageFlags usage, const char *tag, VkBuffer &bufferDst, VkDeviceMemory &memoryDst);
    bool saveNextNormalData = false;

    std::unique_ptr<ModelLoaderBase> activeLoader;
    std::vector<BGLModel::Vertex> normalDataVertices{};
    std::vector<GLTFMaterial> *p_materialSet = nullptr;
    BGLTextureLoader *pTextureLoader = nullptr;
    BGLMaterialManager *pMaterialManager = nullptr;
    BGLSkinManager *pSkinManager = nullptr;

    BGLDevice &bglDevice;
    entt::registry &registry;
};
} // namespace bagel