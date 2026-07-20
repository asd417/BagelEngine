#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include "animation/bagel_animation.hpp" // SkeletonData, Pose, JointTransform (manual posing)
#include "ecs/components/defines.h"
#include "ecs/components/transform.hpp"
#include "engine/bagel_engine_device.hpp"
#include "model/bagel_model.hpp"
#include "model/model_loaders/model_load_settings.hpp"

namespace bagel
{
// Per-entity model instance: a light handle to a shared Model (owned by
// ModelCacheManager) plus this entity's per-instance state. Holds NO GPU
// resources, so destroying the entity frees nothing GPU-side and can never
// dangle another instance's buffers — the fix for the old owner/borrower
// double-free. `model` is resolved from the cache at build/load;
// loadSettings.source is the serialized identity used to re-resolve it.
struct ModelComponent
{
    // Compat aliases so existing `ModelComponent::Submesh` / `::MAX_SUBMESHES`
    // keep working.
    using Submesh = Model::Submesh;
    using SubmeshRange = Model::SubmeshRange;
    static constexpr uint32_t MAX_SUBMESHES = Model::MAX_SUBMESHES;
    static constexpr uint32_t MAX_MATERIALS = Model::MAX_MATERIALS;

    // Shared model (buffers + submeshes + bounds + skin block). Owned by the
    // cache; never freed here. Resolved by ModelComponentBuilder::buildComponent
    // (or on map rehydrate).
    Model *model = nullptr;

    // The build recipe — serialized identity of this instance's model.
    // loadSettings.source is the cache key used to (re)resolve `model` on load.
    ModelLoadSettings loadSettings{};

    // Material recipe for GENERATED models (source paths, indexed by
    // Submesh::materialIndex), kept per-entity so a generated model's authored
    // materials survive save/load. Empty for OBJ/GLTF/LDraw, whose materials come
    // back from the asset. materialCount = valid slots.
    bagel::MaterialSource materialSources[MAX_MATERIALS]{};
    uint32_t materialCount = 0;

    // This entity's selected skin row (per-instance) and frustum-cull toggle.
    uint8_t skinIndex = 0;
    bool frustumCull = true;

    // Direct access to the shared model data (buffers, submeshes, bounds, skin
    // block).
    Model &mesh()
    {
        return *model;
    }
    const Model &mesh() const
    {
        return *model;
    }

    // Switch this entity's skin. Instant (next draw computes a new row base);
    // ignored if out of range. numSkins/skinBase come from the model's
    // "<model>.yaml" sidecar.
    void setSkin(uint32_t i)
    {
        if (model && i < model->numSkins)
            skinIndex = static_cast<uint8_t>(i);
    }

    // Record a generated model's material source for slot `materialIdx` (= a
    // submesh's materialIndex). Serialized; the loader re-bakes textures from
    // these paths on rehydrate.
    void setMaterialSource(uint32_t materialIdx,
                           const bagel::MaterialSource &src)
    {
        assert(materialIdx < MAX_MATERIALS);
        materialSources[materialIdx] = src;
        if (materialIdx + 1 > materialCount)
            materialCount = materialIdx + 1;
    }

    // Submesh ranges + transparency test — forwarded from the shared model.
    SubmeshRange solidSubmeshes() const
    {
        return model->solidSubmeshes();
    }
    SubmeshRange transparentSubmeshes() const
    {
        return model->transparentSubmeshes();
    }
    bool hasTransparent() const
    {
        return model->hasTransparent();
    }
};

struct WireframeComponent
{
    static constexpr uint32_t MAX_SUBMESHES = 128;
    struct Submesh
    {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t materialIndex = 0;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
    };
    ModelLoadSettings loadSettings{};
    Submesh submeshes[MAX_SUBMESHES]{};
    uint32_t submeshCount = 0;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
    bool frustumCull = true;

    // Transient GPU handles — never serialized; rebuilt by the model loader on
    // load. Default to VK_NULL_HANDLE so a default-constructed/half-loaded
    // component is safe to destroy (vkDestroyBuffer/vkFreeMemory on null are
    // no-ops).
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    void *mappedVB = nullptr; // host visible vertex buffer will be mapped here
    void *mappedIB = nullptr; // host visible index buffer will be mapped here

    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};

// Rig + baked identity for a skinned entity (paired with the hot AnimationPlaybackComponent).
// "Cold" only relative to the loops the split targets: it is deliberately kept OUT of
// updateAnimation and the animated render passes, which instead stream the small scalar-only
// playback component. It IS still read every frame by the skeleton-resolve pass
// (ResolveSkeletonGlobals, for attachment parenting) — but that pass fundamentally needs the
// skeleton, so no split makes it lean. Otherwise touched only on load/build and pose editing.
// Built in attachSkinningState.
struct AnimationComponent
{
    // Per-clip baked frame tables (parallel arrays, indexed by clip). The CURRENT clip's window is
    // cached as scalars in AnimationPlaybackComponent for the hot path; these full tables are read
    // only when the clip changes (to refresh that cache) and by the debug UI.
    std::vector<uint32_t> clipFrameBases{};  // per clip: first frame row (in frames)
    std::vector<uint32_t> clipFrameCounts{}; // per clip: baked frame count
    std::vector<std::string>
        clipNames{}; // per clip: glTF animation name (parallel to clipFrameBases)
    // Model-space joint matrices for THIS frame (joint local -> model space),
    // resolved from the current pose by the engine BEFORE the hierarchy pass
    // ("resolve bones before parents") so attachment-parented children read
    // up-to-date bone transforms. Transient (never serialized).
    std::vector<glm::mat4> currentGlobals{};
    // frames/sec the clips were baked at; used by clipDuration(). Mirrored into
    // AnimationPlaybackComponent::fps, which animBaseOffset() reads on the hot path.
    float fps = 60.0f;

    // Manual-posing / IK rig, retained from load so a local pose can be resolved into skinning
    // matrices at runtime. editPose is the per-joint TRS the gizmo authors and is the one field here
    // serialized with the map (see bagel_ecs_serialize.hpp), re-applied after the builder rebuilds
    // the rest on rehydrate. ikSetups are NOT serialized — the "<model>.yaml" sidecar is their
    // single source of truth, re-attached every load.
    SkeletonData skeleton{}; // restPose / parents / inverseBind, retained for runtime resolve
    Pose editPose{};         // per-joint local TRS being authored
    std::vector<IKSetup>
        ikSetups{}; // per-armature IK chains, applied on top of editPose
    uint32_t clipCount() const
    {
        return static_cast<uint32_t>(clipFrameBases.size());
    }
    const char *clipName(uint32_t c) const
    {
        return (c < clipNames.size() && !clipNames[c].empty())
                   ? clipNames[c].c_str()
                   : "(unnamed)";
    }
    // Duration (seconds) of an ARBITRARY clip, read from the cold frame table. The hot
    // AnimationPlaybackComponent::clipDuration() covers the CURRENT clip without a vector deref.
    float clipDuration(uint32_t c) const
    {
        return (c < clipFrameCounts.size() && clipFrameCounts[c] > 0 && fps > 0.0f)
                   ? static_cast<float>(clipFrameCounts[c] - 1) / fps
                   : 0.0f;
    }
};
// HOT half: the lean, per-frame playback state iterated every frame by updateAnimation and the
// animated render passes (AnimatedGBuffer/Shadow). Scalars only, no heap — so many components fit
// per cache line. animBaseOffset() resolves the palette row from the cached current-clip window
// below, which the builder seeds for clip 0 and which must be refreshed from AnimationComponent's
// tables whenever `clip` changes. The heavy rig data lives in the cold AnimationComponent.
struct AnimationPlaybackComponent
{
    uint32_t paletteBase =
        0; // matrix index of (clip 0, frame 0) in the global palette SSBO
    // Cached frame window of the CURRENT clip, copied from AnimationComponent's tables
    // (clipFrameBases[clip] / clipFrameCounts[clip]) so the hot path never dereferences a vector.
    uint32_t clipFrameBase = 0;  // current clip: first frame row
    uint32_t clipFrameCount = 0; // current clip: baked frame count
    uint32_t jointCount = 0;
    float fps = 60.0f; // mirror of AnimationComponent::fps for the hot path

    // Playback state.
    uint32_t clip = 0;
    float time = 0.0f;
    bool playing = true;
    bool loop = true;

    // Manual posing: when manualPose is set, draws read from a dedicated dynamic palette region
    // (dynamicPaletteBase) that the engine fills each frame by resolving the cold component's
    // editPose (+ IK), instead of a baked clip frame. poseDirty gates the re-resolve+upload so we
    // only do it on change. manualPose is the field here serialized with the map; the authored
    // editPose it drives lives in (and is serialized from) AnimationComponent.
    bool manualPose = false; // route draws to the dynamic region when true
    uint32_t dynamicPaletteBase =
        0;                 // base of the reserved jointCount-matrix scratch region
    bool poseDirty = true; // re-resolve + re-upload editPose when set

    // Palette row base for the current clip/time — pushed to the shader as animBaseOffset. Snaps to
    // the nearest baked frame (clamped to the clip's last frame); manual-posed entities read the
    // dynamic region directly.
    uint32_t animBaseOffset() const
    {
        if (manualPose)
            return dynamicPaletteBase; // hand-posed: read the dynamic region
        const uint32_t frames = clipFrameCount;
        uint32_t frame = (fps > 0.0f) ? static_cast<uint32_t>(time * fps) : 0;
        // Clamp into the clip's frame window. With no baked frames (frames==0) there is nothing to
        // index past frame 0 — without this guard the frame index runs away and reads unwritten
        // palette memory (collapsing clip-less skinned meshes to the origin).
        if (frames == 0)
            frame = 0;
        else if (frame >= frames)
            frame = frames - 1;
        return paletteBase + (clipFrameBase + frame) * jointCount;
    }

    // Duration (seconds) of the CURRENT clip, from the cached frame window — hot path, no vector
    // deref (the arbitrary-clip AnimationComponent::clipDuration(c) reads the cold table instead).
    float clipDuration() const
    {
        return (clipFrameCount > 0 && fps > 0.0f)
                   ? static_cast<float>(clipFrameCount - 1) / fps
                   : 0.0f;
    }
};
// Switch the current clip: point the hot playback state at clip `c` and refresh its cached frame
// window from the cold component's per-clip tables (animBaseOffset()/clipDuration() read only those
// cached scalars, never the tables — so the cache MUST be refreshed here on every clip change).
// Resets playback to the clip's start. Out-of-range `c` is ignored.
inline void selectClip(AnimationPlaybackComponent &play, const AnimationComponent &anim,
                       uint32_t c)
{
    if (c >= anim.clipCount())
        return;
    play.clip = c;
    play.time = 0.0f;
    play.clipFrameBase = anim.clipFrameBases[c];
    play.clipFrameCount = anim.clipFrameCounts[c];
}
// Named bone attach points baked from the model's "<model>.yaml" sidecar — the
// Source-engine $attachment analog. Each point is a local offset (translation +
// rotation) within a skeleton bone's space; its world transform is bone_world *
// localOffset. Transient: the model builder rebuilds it from the sidecar on
// load/rehydrate (not serialized — the sidecar owns it). Query via
// lookupAttachment() / getAttachmentWorld() in bagel_hierachy.hpp. A
// TransformHierachyComponent can name one of these to parent a child to the
// point.
struct AttachmentComponent
{
    struct Point
    {
        char name[MAX_ATTACHMENT_NAME] =
            {};                      // referenced by
                                     // TransformHierachyComponent::attachment
                                     // / code; zero-init = valid empty
                                     // C-string before it's filled
        int joint = -1;              // skeleton joint the point rides (resolved from bone name)
        glm::mat4 localOffset{1.0f}; // offset within the bone's local space
    };
    std::vector<Point> points;

    // Attachment name -> index into points (or -1). Mirrors Source's
    // LookupAttachment.
    int lookup(const std::string &name) const
    {
        for (size_t i = 0; i < points.size(); ++i)
            if (points[i].name == name)
                return static_cast<int>(i);
        return -1;
    }
};
} // namespace bagel
