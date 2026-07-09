#pragma once
#include <vector>
#include <string>
#include <tuple>
#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "texture/bagel_textures.hpp"
#include "model/bagel_model.hpp"
#include "animation/bagel_animation.hpp" // JointTransform, SkeletonData, AnimationClip, IKSetup
#include "ecs/components/model.hpp"
#include <xatlas.h>


namespace bagel {


    class BGLMaterialManager; // defined in bagel_material.hpp; used here only by pointer

    class ModelLoaderBase
    {
	public:
        ModelLoaderBase(BGLTextureLoader* pTL = nullptr) : pTextureLoader(pTL){};
        virtual ~ModelLoaderBase() = default;
        void setMaterialManager(BGLMaterialManager* mm) { pMaterialManager = mm; }
        //first use textureloader to place image textures into the descriptors and populate materials vector with descriptor handles
        //populate submeshes, vertices, indices
        virtual void load(ModelLoadSettings parms) = 0;
        std::vector<SubmeshInfo> &getSubmeshes() { return submeshes; };
        std::vector<BGLModel::Vertex> &getVertices() { return vertices; };
        std::vector<uint32_t> &getIndices(){return indices; };
		// xatlas is a uv packing library.
		// the goal is to have a separate program that can be run independently to generate lightmaps and save to disk
		// the level compiler/editor will likely share the code with the main engine therefore this function exists here
		xatlas::Atlas* createLightMapAtlas();
		// see: https://github.com/jpcy/xatlas/blob/master/source/examples/example.cpp
		virtual void generateLightMapAtlas();

        bool hasTangents() const { return tangentsLoaded; }
		void calculateTangent();

        // Skin block this model owns in the global skin table, how many material slots
        // (= columns) it has, and how many skins (rows). The builder copies these onto the
        // ModelComponent.
        uint32_t getSkinBase()  const { return skinBase; }
        uint32_t getNumSlots()  const { return numSlots; }
        uint32_t getNumSkins()  const { return numSkins; }

        // Skeletal animation (bone skinning). isSkinned() is true once a loaded primitive
        // carried JOINTS_0/WEIGHTS_0. skinInfluences is parallel to getVertices() (one entry
        // per vertex, same order) and becomes the per-vertex SSBO; skeleton feeds the baker.
        bool isSkinned() const { return !skinInfluences.empty(); }
        std::vector<SkinInfluence>&         getSkinInfluences() { return skinInfluences; }
        const SkeletonData&                 getSkeleton()   const { return skeleton; }
        const std::vector<AnimationClip>&   getAnimations() const { return animations; }

        // Resolve the sidecar's authored IK chains (bone NAMES) into IKSetups (joint INDICES) using
        // the parsed skeleton's joint names. Call after the skeleton is parsed (skeleton.names set).
        // Unknown bone names drop that chain with a warning. Empty when the sidecar lists no IK.
        std::vector<IKSetup> resolveIkSetups() const;

        // Resolve the sidecar's authored attachments (bone NAMES + local offset) into runtime attach
        // points (joint INDICES + offset matrix). Same timing/rules as resolveIkSetups.
        std::vector<AttachmentComponent::Point> resolveAttachments() const;
    protected:
        // Reserve this model's skin block and fill it. numSlots = distinct material count;
        // numSkins comes from the "<model>.yaml" sidecar ($texturegroup analog), or 1 if
        // absent. For skin s, slot k: the sidecar override if present, else the model's own
        // material[k]. Call after `materials` is populated and BEFORE building vertices, so
        // each vertex can store its local slot index. modelSourcePath is the engine-relative
        // model path used to find the sidecar.
        void buildSkinBlock(const std::string& modelSourcePath);
        // Local material id -> local slot (the column the vertex stores). Clamped; out of
        // range -> 0 (the unused material).
        uint16_t localMaterialSlot(int localMaterialId) const {
            return (localMaterialId >= 0 && localMaterialId < static_cast<int>(numSlots))
                ? static_cast<uint16_t>(localMaterialId) : 0;
        }

        BGLTextureLoader* pTextureLoader;
        BGLMaterialManager* pMaterialManager = nullptr;
        std::vector<SubmeshInfo> submeshes;
		std::vector<uint32_t> indices{};
        std::vector<BGLModel::Vertex> vertices{};
        std::vector<BGLModel::Material> materials{};
        uint32_t skinBase = 0;  // base entry of this model's skin block
        uint32_t numSlots = 1;  // distinct material count (columns); always >= 1
        uint32_t numSkins = 1;  // skin rows from the sidecar; always >= 1
        bool tangentsLoaded = false;

        // Skeletal skinning data (empty for static models). skinInfluences is kept parallel
        // to `vertices`; loaders that read JOINTS_0/WEIGHTS_0 must keep both in lockstep.
        std::vector<SkinInfluence> skinInfluences{};
        SkeletonData               skeleton{};
        std::vector<AnimationClip> animations{};
        // Authored IK chains (bone names) read from the sidecar in buildSkinBlock; resolved to
        // joint indices later via resolveIkSetups() once skeleton.names is populated.
        std::vector<ModelSidecar::IkChain> ikChains{};
        // Authored attach points (bone names) from the sidecar; resolved via resolveAttachments().
        std::vector<ModelSidecar::Attachment> attachmentDefs{};
    };
}