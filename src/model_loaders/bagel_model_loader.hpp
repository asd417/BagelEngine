#pragma once
#include <vector>
#include <string>
#include <tuple>
#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "bagel_textures.hpp"
#include "model_load_settings.hpp"
#include "model_sidecar.hpp"             // ModelSidecar::IkChain (authored IK, resolved at build)
#include "animation/bagel_animation.hpp" // JointTransform, SkeletonData, AnimationClip, IKSetup

#include <xatlas.h>


namespace bagel {
    // Magic hash function from boost, modified to return same value for same vertex
	inline void Hash(std::size_t& seed, const float& v)
	{
		//std::hash<T> hasher;
		seed ^= static_cast<uint32_t>(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}
    struct SubmeshInfo {
			uint32_t firstIndex   = 0;
			uint32_t indexCount   = 0;
			uint32_t firstVertex  = 0;
			uint32_t vertexCount  = 0;
			uint32_t materialIndex = 0;
			bool transparentMaterial = false;
		};

	// Per-vertex skinning influence, packed to 8 bytes. NOT part of BGLModel::Vertex —
	// it lives in a parallel array that becomes a GPU SSBO read by the skinned shader via
	// skinData[skinBase + gl_VertexIndex]. The static vertex format is untouched.
	//   joints[i]  = joint index (into the skeleton's palette); glTF caps influences at 4.
	//   weights[i] = unorm8 blend weight (255 == 1.0). Default: full weight on joint 0.
	struct SkinInfluence {
		uint8_t joints[4]{ 0, 0, 0, 0 };
		uint8_t weights[4]{ 255, 0, 0, 0 };
	};

	// SkeletonData / JointTransform / AnimationClip live in animation/bagel_animation.hpp
	// (included above) — the loader fills them, the animation module consumes them.
    // ComponentBuildMode and ModelLoadSettings now live in model_load_settings.hpp
	// (included above) so ModelComponent can store/serialize the load recipe.
	namespace BGLModel
	{
        struct Material {
            // bindless texture indices — 0 means unused. Only the 4 maps the shaders sample.
			uint16_t albedoMap     = 0;
			uint16_t normalMap     = 0;
			uint16_t metalRoughMap = 0; // G=roughness, B=metallic (glTF ORM convention)
			uint16_t emissionMap   = 0;
        };
        struct Vertex {
			glm::vec3 position;
			glm::vec3 color;
			glm::vec3 normal;
			glm::vec4 tangent{ 0.0f,0.0f,0.0f,1.0f }; // xyz = tangent, w = bitangent handedness sign (+1 or -1)
			glm::vec2 uv;

			// Index into the global material table (BGLMaterialManager). The vertex shader
			// resolves it to the actual bindless texture handles. 0 = the all-unused material.
			uint16_t materialIndex = 0;

			static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
			static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

			bool operator=(const Vertex& other) const {
				return std::tie(
					position.x,position.y,position.z,
					normal.x,normal.y,normal.z) 
					== std::tie(other.position.x, other.position.y, other.position.z,
						other.normal.x, other.normal.y, other.normal.z);
			}
			bool operator<(const Vertex& other) const {
				return std::tie(
					position.x, position.y, position.z,
					normal.x, normal.y, normal.z)
					< std::tie(other.position.x, other.position.y, other.position.z,
						other.normal.x, other.normal.y, other.normal.z);
			}
			bool operator>(const Vertex& other) const {
				return std::tie(
					position.x, position.y, position.z,
					normal.x, normal.y, normal.z)
					> std::tie(other.position.x, other.position.y, other.position.z,
						other.normal.x, other.normal.y, other.normal.z);
			}
			
		};
		
		struct VertexHasher
		{
			size_t operator()(const Vertex& a) const
			{
				size_t seed{};
				Hash(seed, a.position.x);
				Hash(seed, a.position.y);
				Hash(seed, a.position.z);
				Hash(seed, a.normal.x);
				Hash(seed, a.normal.y);
				Hash(seed, a.normal.z);
				return seed;
			}
		};

		//We assume that two vertices with same vertex normal and same uv coordinate at same position are same vertices 
		struct VertexEquals
		{
			bool operator()(const Vertex& a, const Vertex& b) const
			{
				return std::tie(
					a.position.x, a.position.y, a.position.z,
					a.normal.x, a.normal.y, a.normal.z,
					a.uv.x,a.uv.y)
					== std::tie(b.position.x, b.position.y, b.position.z,
						b.normal.x, b.normal.y, b.normal.z,
						b.uv.x, b.uv.y);
			}
		};
	};

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