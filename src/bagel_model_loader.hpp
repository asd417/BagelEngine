#pragma once
#include <vector>
#include <string>
#include <tuple>
#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "bagel_textures.hpp"

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
    //Model-related Component Build Mode
	//Determines the vertex buffer layout
	enum ComponentBuildMode {
		FACES,
		LINES
	};
	struct ModelLoadSettings {
        std::string source;
        float scale = 1.0f;
		glm::vec3 scaleVec = {1.0f, 1.0f, 1.0f};
		ComponentBuildMode buildMode = ComponentBuildMode::FACES;
		uint32_t maxPrimitives = UINT32_MAX;
		// When true (default) all solid/opaque geometry is merged into a single submesh.
		// When false, each source mesh's solid geometry is kept as its own submesh — useful
		// for large models where one merged submesh would be too big. Transparent geometry is
		// always split per source mesh regardless of this setting.
		bool mergeSolidSubmeshes = true;
		ModelLoadSettings() = default;
		ModelLoadSettings(ComponentBuildMode mode) : buildMode(mode) {}
	};
	namespace BGLModel 
	{
        struct Material {
            // texture indices — max 65535 slots; 0 means unused
			uint16_t albedoMap     = 0;
			uint16_t normalMap     = 0;
			uint16_t metalRoughMap = 0; // G=roughness, B=metallic (glTF ORM convention)
			uint16_t specularMap   = 0;
			uint16_t heightMap     = 0;
			uint16_t opacityMap    = 0;
			uint16_t aoMap         = 0;
			uint16_t refractionMap = 0;
			uint16_t emissionMap   = 0;
        };
        struct Vertex {
			glm::vec3 position;
			glm::vec3 color;
			glm::vec3 normal;
			glm::vec4 tangent{ 0.0f,0.0f,0.0f,1.0f }; // xyz = tangent, w = bitangent handedness sign (+1 or -1)
			glm::vec2 uv;

			// texture indices — max 65535 slots; 0 means unused
			uint16_t albedoMap     = 0;
			uint16_t normalMap     = 0;
			uint16_t metalRoughMap = 0; // G=roughness, B=metallic (glTF ORM convention)
			uint16_t specularMap   = 0;
			uint16_t heightMap     = 0;
			uint16_t opacityMap    = 0;
			uint16_t aoMap         = 0;
			uint16_t refractionMap = 0;
			uint16_t emissionMap   = 0;
			
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
    class ModelLoaderBase
    {
    public:
        ModelLoaderBase(BGLTextureLoader* pTL = nullptr) : pTextureLoader(pTL){};
        virtual ~ModelLoaderBase() = default;
        //first use textureloader to place image textures into the descriptors and populate materials vector with descriptor handles
        //populate submeshes, vertices, indices
        virtual void load(ModelLoadSettings parms) = 0;
        std::vector<SubmeshInfo> &getSubmeshes() { return submeshes; };
        std::vector<BGLModel::Vertex> &getVertices() { return vertices; };
        std::vector<uint32_t> &getIndices(){return indices; };
        void calculateTangent();
        bool hasTangents() const { return tangentsLoaded; }
    protected:
        BGLTextureLoader* pTextureLoader;
        std::vector<SubmeshInfo> submeshes;
		std::vector<uint32_t> indices{};
        std::vector<BGLModel::Vertex> vertices{};
        std::vector<BGLModel::Material> materials{};
        bool tangentsLoaded = false;
    };
}