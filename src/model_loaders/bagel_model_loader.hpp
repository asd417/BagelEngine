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
    protected:
        // Register every entry of `materials` into the global material table and fill
        // materialIndexMap (local material id -> global table index). Call after `materials`
        // is populated and BEFORE building vertices, so each vertex can store its global
        // index. Local id 0..n map through materialIndexMap; "no material" -> global 0.
        void registerMaterialsToTable();
        uint16_t globalMaterialIndex(int localMaterialId) const {
            return (localMaterialId >= 0 && localMaterialId < static_cast<int>(materialIndexMap.size()))
                ? materialIndexMap[localMaterialId] : 0;
        }

        BGLTextureLoader* pTextureLoader;
        BGLMaterialManager* pMaterialManager = nullptr;
        std::vector<SubmeshInfo> submeshes;
		std::vector<uint32_t> indices{};
        std::vector<BGLModel::Vertex> vertices{};
        std::vector<BGLModel::Material> materials{};
        std::vector<uint16_t> materialIndexMap{}; // local material id -> global table index
        bool tangentsLoaded = false;
    };
}