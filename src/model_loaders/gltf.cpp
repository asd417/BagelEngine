#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "stb_image.h"
#include "gltf.hpp"
#include <filesystem>
#include "../bagel_util.hpp"

namespace bagel
{
	GLTFModelLoader::GLTFModelLoader(BGLTextureLoader* pTL) : ModelLoaderBase(pTL)
	{};

	void GLTFModelLoader::load(ModelLoadSettings parms)
	{
		loadGLTFModel(parms.source.c_str(), parms.maxPrimitives);
	}

	void GLTFModelLoader::loadGLTFModel(const char *filename, uint32_t maxPrimitives)
	{
		std::cout << "Loading glTF model " << filename << "\n";
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		// We load textures ourselves via BGLTextureLoader; suppress tiny_gltf's image loader
		loader.SetImageLoader(
			[](tinygltf::Image*, const int, std::string*, std::string*,
			   int, int, const unsigned char*, int, void*) -> bool { return true; },
			nullptr);
		std::string err;
		std::string warn;
		bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, util::enginePath(filename));

		if (!warn.empty()) printf("glTF warn: %s\n", warn.c_str());
		if (!err.empty())  printf("glTF err: %s\n",  err.c_str());
		if (!ret)
		{
			printf("Failed to parse glTF\n");
			throw std::runtime_error("Failed to parse glTF");
		}

		std::cout << "\t" << model.meshes.size() << " meshes, "
		          << model.materials.size() << " materials\n";

		// Engine-relative directory of this file: "/models/sponza/Sponza.gltf" → "/models/sponza"
		std::string fileStr(filename);
		std::string modelDir = fileStr.substr(0, fileStr.find_last_of('/'));

		// Build material table
		materials.resize(model.materials.size());
		for (size_t i = 0; i < model.materials.size(); i++)
		{
			const auto &mat = model.materials[i];
			const auto &pbr = mat.pbrMetallicRoughness;

			auto tryLoadGLTFTexture = [&](int texIdx, VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB) -> uint16_t
			{
				if (texIdx < 0) return 0;
				const tinygltf::Image &img = model.images[model.textures[texIdx].source];
				if (!img.uri.empty())
				{
					// External file: URI is relative to the .gltf directory
					std::string engineRelPath = modelDir + "/" + img.uri;
					return pTextureLoader ? static_cast<uint16_t>(pTextureLoader->loadTexture(engineRelPath.c_str(), fmt)) : 0;
				}
				// Packed image (GLB bufferView or base64): requires loadTextureFromMemory (not yet implemented)
				std::cerr << "[GLTFModelLoader] packed texture (image "
				          << model.textures[texIdx].source << ") not yet supported\n";
				return 0;
			};

			// Albedo/emission are sRGB color data; normal, metalRough, AO are linear data
			materials[i].albedoMap     = tryLoadGLTFTexture(pbr.baseColorTexture.index,          VK_FORMAT_R8G8B8A8_SRGB);
			materials[i].normalMap     = tryLoadGLTFTexture(mat.normalTexture.index,              VK_FORMAT_R8G8B8A8_UNORM);
			materials[i].metalRoughMap = tryLoadGLTFTexture(pbr.metallicRoughnessTexture.index,  VK_FORMAT_R8G8B8A8_UNORM);
			materials[i].emissionMap   = tryLoadGLTFTexture(mat.emissiveTexture.index,            VK_FORMAT_R8G8B8A8_SRGB);
			materials[i].aoMap         = tryLoadGLTFTexture(mat.occlusionTexture.index,           VK_FORMAT_R8G8B8A8_UNORM);
		}

		for (const tinygltf::Mesh &mesh : model.meshes)
		{
			loadGLTFMesh(model, mesh, maxPrimitives);
		}
		if (!tangentsLoaded)
			printf("[GLTF] No TANGENT attribute found — tangents will be computed\n");
	}

	void GLTFModelLoader::loadGLTFMesh(const tinygltf::Model &model, const tinygltf::Mesh &mesh, uint32_t maxPrimitives)
	{
		for (size_t i = 0; i < mesh.primitives.size() && submeshes.size() < maxPrimitives; i++)
		{
			SubmeshInfo smi{};
			uint32_t firstIndex  = static_cast<uint32_t>(indices.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertices.size());

			const tinygltf::Primitive &prim = mesh.primitives[i];
			int material_idx = prim.material;

			// Resolve per-primitive material handles (same for all vertices in this primitive)
			BGLModel::Material primMat{};
			if (material_idx >= 0 && material_idx < static_cast<int>(materials.size()))
				primMat = materials[material_idx];

			uint32_t indexCount = 0;
			{
				const float *positionBuffer  = nullptr;
				const float *normalsBuffer   = nullptr;
				const float *texCoordsBuffer = nullptr;
				const float *tangentBuffer   = nullptr;
				size_t vertexCount = 0;

				if (prim.attributes.find("POSITION") != prim.attributes.end())
				{
					const tinygltf::Accessor   &acc  = model.accessors[prim.attributes.find("POSITION")->second];
					const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
					positionBuffer = reinterpret_cast<const float *>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
					vertexCount = acc.count;
				}
				if (prim.attributes.find("NORMAL") != prim.attributes.end())
				{
					const tinygltf::Accessor   &acc  = model.accessors[prim.attributes.find("NORMAL")->second];
					const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
					normalsBuffer = reinterpret_cast<const float *>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
				}
				if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end())
				{
					const tinygltf::Accessor   &acc  = model.accessors[prim.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
					texCoordsBuffer = reinterpret_cast<const float *>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
				}
				if (prim.attributes.find("TANGENT") != prim.attributes.end())
				{
					const tinygltf::Accessor   &acc  = model.accessors[prim.attributes.find("TANGENT")->second];
					const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
					tangentBuffer = reinterpret_cast<const float *>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
					if (!tangentsLoaded) printf("[GLTF] TANGENT attribute found — loading from file\n");
					tangentsLoaded = true;
				}

				for (size_t v = 0; v < vertexCount; v++)
				{
					BGLModel::Vertex vert{};
					vert.position = glm::vec3(positionBuffer[v * 3], positionBuffer[v * 3 + 1], positionBuffer[v * 3 + 2]);
					vert.normal   = glm::normalize(normalsBuffer
						? glm::vec3(normalsBuffer[v * 3], normalsBuffer[v * 3 + 1], normalsBuffer[v * 3 + 2])
						: glm::vec3(0.0f));
					vert.uv    = texCoordsBuffer ? glm::vec2(texCoordsBuffer[v * 2], texCoordsBuffer[v * 2 + 1]) : glm::vec2(0.0f);
					vert.color = glm::vec3(1.0f);
					if (tangentBuffer)
						vert.tangent = glm::vec4(tangentBuffer[v * 4], tangentBuffer[v * 4 + 1], tangentBuffer[v * 4 + 2], tangentBuffer[v * 4 + 3]);

					vert.albedoMap    = primMat.albedoMap;
					vert.normalMap    = primMat.normalMap;
					vert.metalRoughMap = primMat.metalRoughMap;
					vert.emissionMap  = primMat.emissionMap;
					vert.aoMap        = primMat.aoMap;
					vert.specularMap  = primMat.specularMap;
					vert.heightMap    = primMat.heightMap;
					vert.opacityMap   = primMat.opacityMap;
					vert.refractionMap = primMat.refractionMap;

					vertices.push_back(vert);
				}

				const tinygltf::Accessor   &idxAcc  = model.accessors[prim.indices];
				const tinygltf::BufferView &idxView  = model.bufferViews[idxAcc.bufferView];
				const tinygltf::Buffer     &idxBuf   = model.buffers[idxView.buffer];
				indexCount = static_cast<uint32_t>(idxAcc.count);

				switch (idxAcc.componentType)
				{
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
				{
					const uint32_t *buf = reinterpret_cast<const uint32_t *>(&idxBuf.data[idxAcc.byteOffset + idxView.byteOffset]);
					for (size_t idx = 0; idx < idxAcc.count; idx++)
						indices.push_back(buf[idx] + vertexStart);
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
				{
					const uint16_t *buf = reinterpret_cast<const uint16_t *>(&idxBuf.data[idxAcc.byteOffset + idxView.byteOffset]);
					for (size_t idx = 0; idx < idxAcc.count; idx++)
						indices.push_back(static_cast<uint32_t>(buf[idx]) + vertexStart);
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
				{
					const uint8_t *buf = reinterpret_cast<const uint8_t *>(&idxBuf.data[idxAcc.byteOffset + idxView.byteOffset]);
					for (size_t idx = 0; idx < idxAcc.count; idx++)
						indices.push_back(static_cast<uint32_t>(buf[idx]) + vertexStart);
					break;
				}
				default:
					std::cerr << "Unsupported index component type: " << idxAcc.componentType << "\n";
					return;
				}
			}

			smi.firstIndex  = firstIndex;
			smi.indexCount  = indexCount;
			smi.firstVertex = vertexStart;
			smi.vertexCount = static_cast<uint32_t>(vertices.size()) - vertexStart;
			smi.materialIndex = material_idx;
			submeshes.push_back(smi);
		}
	}
}
