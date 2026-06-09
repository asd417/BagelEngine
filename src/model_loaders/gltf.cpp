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

	uint16_t GLTFModelLoader::tryLoadGLTFTexture(const tinygltf::Model& model, const std::string& modelDir, int texIdx, VkFormat fmt)
	{
		if (texIdx < 0) return 0;
		const tinygltf::Image& img = model.images[model.textures[texIdx].source];
		if (!img.uri.empty())
		{
			std::string engineRelPath = modelDir + "/" + img.uri;
			return pTextureLoader ? static_cast<uint16_t>(pTextureLoader->loadTexture(engineRelPath.c_str(), fmt)) : 0;
		}
		// Packed image (GLB bufferView or base64): requires loadTextureFromMemory (not yet implemented)
		std::cerr << "[GLTFModelLoader] packed texture (image "
		          << model.textures[texIdx].source << ") not yet supported\n";
		return 0;
	}

	bool GLTFModelLoader::isTransparent(const tinygltf::Material& mat)
	{
		// BLEND: full alpha blending; MASK: binary cut-out transparency
		if (mat.alphaMode == "BLEND" || mat.alphaMode == "MASK")
			return true;
		// Uniform base color alpha < 1 without a texture also indicates transparency
		if (mat.pbrMetallicRoughness.baseColorFactor.size() == 4 &&
		    mat.pbrMetallicRoughness.baseColorFactor[3] < 1.0)
			return true;
		return false;
	}

	void GLTFModelLoader::appendPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& prim)
	{
		uint32_t vertexStart = static_cast<uint32_t>(vertices.size());

		const float* positionBuffer  = nullptr;
		const float* normalsBuffer   = nullptr;
		const float* texCoordsBuffer = nullptr;
		const float* tangentBuffer   = nullptr;
		size_t vertexCount = 0;

		if (prim.attributes.find("POSITION") != prim.attributes.end())
		{
			const tinygltf::Accessor&   acc  = model.accessors[prim.attributes.find("POSITION")->second];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			positionBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
			vertexCount = acc.count;
		}
		if (prim.attributes.find("NORMAL") != prim.attributes.end())
		{
			const tinygltf::Accessor&   acc  = model.accessors[prim.attributes.find("NORMAL")->second];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			normalsBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
		}
		if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end())
		{
			const tinygltf::Accessor&   acc  = model.accessors[prim.attributes.find("TEXCOORD_0")->second];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			texCoordsBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
		}
		if (prim.attributes.find("TANGENT") != prim.attributes.end())
		{
			const tinygltf::Accessor&   acc  = model.accessors[prim.attributes.find("TANGENT")->second];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			tangentBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
			if (!tangentsLoaded) printf("[GLTF] TANGENT attribute found — loading from file\n");
			tangentsLoaded = true;
		}

		BGLModel::Material primMat{};
		int material_idx = prim.material;
		if (material_idx >= 0 && material_idx < static_cast<int>(materials.size()))
			primMat = materials[material_idx];

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

			vert.albedoMap     = primMat.albedoMap;
			vert.normalMap     = primMat.normalMap;
			vert.metalRoughMap = primMat.metalRoughMap;
			vert.emissionMap   = primMat.emissionMap;
			vert.aoMap         = primMat.aoMap;
			vert.specularMap   = primMat.specularMap;
			vert.heightMap     = primMat.heightMap;
			vert.opacityMap    = primMat.opacityMap;
			vert.refractionMap = primMat.refractionMap;

			vertices.push_back(vert);
		}

		const tinygltf::Accessor&   idxAcc = model.accessors[prim.indices];
		const tinygltf::BufferView& idxView = model.bufferViews[idxAcc.bufferView];
		const tinygltf::Buffer&     idxBuf  = model.buffers[idxView.buffer];

		switch (idxAcc.componentType)
		{
		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
		{
			const uint32_t* buf = reinterpret_cast<const uint32_t*>(&idxBuf.data[idxAcc.byteOffset + idxView.byteOffset]);
			for (size_t i = 0; i < idxAcc.count; i++)
				indices.push_back(buf[i] + vertexStart);
			break;
		}
		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
		{
			const uint16_t* buf = reinterpret_cast<const uint16_t*>(&idxBuf.data[idxAcc.byteOffset + idxView.byteOffset]);
			for (size_t i = 0; i < idxAcc.count; i++)
				indices.push_back(static_cast<uint32_t>(buf[i]) + vertexStart);
			break;
		}
		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
		{
			const uint8_t* buf = reinterpret_cast<const uint8_t*>(&idxBuf.data[idxAcc.byteOffset + idxView.byteOffset]);
			for (size_t i = 0; i < idxAcc.count; i++)
				indices.push_back(static_cast<uint32_t>(buf[i]) + vertexStart);
			break;
		}
		default:
			std::cerr << "Unsupported index component type: " << idxAcc.componentType << "\n";
			break;
		}
	}

	void GLTFModelLoader::loadGLTFModel(const char* filename, uint32_t /*maxPrimitives*/)
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
			const auto& mat = model.materials[i];
			const auto& pbr = mat.pbrMetallicRoughness;
			// Albedo/emission are sRGB color data; normal, metalRough, AO are linear data
			materials[i].albedoMap     = tryLoadGLTFTexture(model, modelDir, pbr.baseColorTexture.index,         VK_FORMAT_R8G8B8A8_SRGB);
			materials[i].normalMap     = tryLoadGLTFTexture(model, modelDir, mat.normalTexture.index,             VK_FORMAT_R8G8B8A8_UNORM);
			materials[i].metalRoughMap = tryLoadGLTFTexture(model, modelDir, pbr.metallicRoughnessTexture.index, VK_FORMAT_R8G8B8A8_UNORM);
			materials[i].emissionMap   = tryLoadGLTFTexture(model, modelDir, mat.emissiveTexture.index,           VK_FORMAT_R8G8B8A8_SRGB);
			materials[i].aoMap         = tryLoadGLTFTexture(model, modelDir, mat.occlusionTexture.index,          VK_FORMAT_R8G8B8A8_UNORM);
		}

		auto primIsTransparent = [&](const tinygltf::Primitive& prim) -> bool {
			return prim.material >= 0
			    && prim.material < static_cast<int>(model.materials.size())
			    && isTransparent(model.materials[prim.material]);
		};

		// Pass 1: merge all opaque primitives across all meshes into submesh 0
		submeshes.push_back({});
		SubmeshInfo& opaqueSM = submeshes[0];
		opaqueSM.firstIndex  = 0;
		opaqueSM.firstVertex = 0;
		opaqueSM.transparentMaterial = false;

		for (const tinygltf::Mesh& mesh : model.meshes)
			for (const tinygltf::Primitive& prim : mesh.primitives)
				if (!primIsTransparent(prim))
					appendPrimitive(model, prim);

		opaqueSM.indexCount  = static_cast<uint32_t>(indices.size());
		opaqueSM.vertexCount = static_cast<uint32_t>(vertices.size());

		// Pass 2: one submesh per mesh for its transparent primitives
		for (const tinygltf::Mesh& mesh : model.meshes)
		{
			bool hasTransparent = false;
			for (const tinygltf::Primitive& prim : mesh.primitives)
				if (primIsTransparent(prim)) { hasTransparent = true; break; }
			if (!hasTransparent) continue;

			SubmeshInfo tsm{};
			tsm.firstIndex  = static_cast<uint32_t>(indices.size());
			tsm.firstVertex = static_cast<uint32_t>(vertices.size());
			tsm.transparentMaterial = true;

			for (const tinygltf::Primitive& prim : mesh.primitives)
				if (primIsTransparent(prim))
					appendPrimitive(model, prim);

			tsm.indexCount  = static_cast<uint32_t>(indices.size()) - tsm.firstIndex;
			tsm.vertexCount = static_cast<uint32_t>(vertices.size()) - tsm.firstVertex;
			submeshes.push_back(tsm);
		}

		if (!tangentsLoaded)
			printf("[GLTF] No TANGENT attribute found — tangents will be computed\n");
	}
}
