#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "stb_image.h"
#include "gltf.hpp"
#include <filesystem>
#include <unordered_map>
#include "../bagel_util.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace bagel
{
	GLTFModelLoader::GLTFModelLoader(BGLTextureLoader* pTL) : ModelLoaderBase(pTL)
	{};

	void GLTFModelLoader::load(ModelLoadSettings parms)
	{
		loadGLTFModel(parms.source.c_str(), parms.mergeSolidSubmeshes);
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
		// Packed image (GLB bufferView): the encoded PNG/JPG bytes live in the binary chunk.
		// Decode them with stb and upload from memory (embedded images have no file path).
		const int imgIdx = model.textures[texIdx].source;
		if (pTextureLoader && img.bufferView >= 0)
		{
			const tinygltf::BufferView& bv = model.bufferViews[img.bufferView];
			const unsigned char* enc = model.buffers[bv.buffer].data.data() + bv.byteOffset;
			int w = 0, h = 0, comp = 0;
			unsigned char* px = stbi_load_from_memory(enc, static_cast<int>(bv.byteLength), &w, &h, &comp, STBI_rgb_alpha);
			if (!px)
			{
				std::cerr << "[GLTFModelLoader] failed to decode embedded image " << imgIdx
				          << ": " << stbi_failure_reason() << "\n";
				return 0;
			}
			// Embedded images have no path; dedup by (model dir, image index).
			const std::string name = modelDir + "#img" + std::to_string(imgIdx);
			const uint16_t handle = static_cast<uint16_t>(pTextureLoader->loadTextureFromMemory(
				name.c_str(), px, static_cast<uint32_t>(w), static_cast<uint32_t>(h), fmt));
			stbi_image_free(px);
			return handle;
		}
		std::cerr << "[GLTFModelLoader] image " << imgIdx << " has neither uri nor bufferView\n";
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

		// Skinning: JOINTS_0 (VEC4 of UNSIGNED_BYTE/SHORT) and WEIGHTS_0 (VEC4 of FLOAT or
		// normalized byte/short). Both are raw byte pointers because component type varies.
		const unsigned char* jointsBuffer  = nullptr; int jointsCompType  = 0;
		const unsigned char* weightsBuffer = nullptr; int weightsCompType = 0;
		if (prim.attributes.find("JOINTS_0") != prim.attributes.end())
		{
			const tinygltf::Accessor&   acc  = model.accessors[prim.attributes.find("JOINTS_0")->second];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			jointsBuffer   = &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
			jointsCompType = acc.componentType;
		}
		if (prim.attributes.find("WEIGHTS_0") != prim.attributes.end())
		{
			const tinygltf::Accessor&   acc  = model.accessors[prim.attributes.find("WEIGHTS_0")->second];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			weightsBuffer   = &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
			weightsCompType = acc.componentType;
		}

		const uint16_t primMaterialIndex = localMaterialSlot(prim.material);

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

			vert.materialIndex = primMaterialIndex;

			vertices.push_back(vert);
		}

		// Fill the parallel skin array for this primitive. resize() grows to the current
		// vertex count, default-filling (weight 1 on joint 0) any earlier non-skinned
		// primitives so skinInfluences stays index-parallel to `vertices`.
		if (jointsBuffer && weightsBuffer)
		{
			auto readJoint = [](const unsigned char* base, size_t v, int c, int ct) -> uint32_t {
				if (ct == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					return reinterpret_cast<const uint16_t*>(base)[v * 4 + c];
				return base[v * 4 + c]; // UNSIGNED_BYTE
			};
			auto readWeight = [](const unsigned char* base, size_t v, int c, int ct) -> float {
				switch (ct) {
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  return base[v * 4 + c] / 255.0f;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return reinterpret_cast<const uint16_t*>(base)[v * 4 + c] / 65535.0f;
				default:                                     return reinterpret_cast<const float*>(base)[v * 4 + c]; // FLOAT
				}
			};

			skinInfluences.resize(vertices.size());
			for (size_t v = 0; v < vertexCount; v++)
			{
				SkinInfluence& si = skinInfluences[vertexStart + v];
				for (int c = 0; c < 4; c++)
				{
					uint32_t j = readJoint(jointsBuffer, v, c, jointsCompType);
					si.joints[c] = j > 255u ? 255u : static_cast<uint8_t>(j);
				}
				float w[4];
				float sum = 0.0f;
				for (int c = 0; c < 4; c++) { w[c] = readWeight(weightsBuffer, v, c, weightsCompType); sum += w[c]; }
				if (sum <= 0.0f) { w[0] = 1.0f; sum = 1.0f; } // degenerate: pin to joint 0
				for (int c = 0; c < 4; c++)
				{
					int q = static_cast<int>(w[c] / sum * 255.0f + 0.5f);
					si.weights[c] = static_cast<uint8_t>(q < 0 ? 0 : q > 255 ? 255 : q);
				}
			}
		}

		const tinygltf::Accessor&   idxAcc = model.accessors[prim.indices];
		const tinygltf::BufferView& idxView = model.bufferViews[idxAcc.bufferView];
		const tinygltf::Buffer&     idxBuf  = model.buffers[idxView.buffer];
		const unsigned char* idxData = &idxBuf.data[idxAcc.byteOffset + idxView.byteOffset];

		auto pushIndices = [&](auto* buf) {
			for (size_t i = 0; i < idxAcc.count; i++)
				indices.push_back(static_cast<uint32_t>(buf[i]) + vertexStart);
		};
		switch (idxAcc.componentType)
		{
		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:   pushIndices(reinterpret_cast<const uint32_t*>(idxData)); break;
		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: pushIndices(reinterpret_cast<const uint16_t*>(idxData)); break;
		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:  pushIndices(reinterpret_cast<const uint8_t*>(idxData));  break;
		default:
			std::cerr << "Unsupported index component type: " << idxAcc.componentType << "\n";
			break;
		}
	}

	// glTF node local transform as TRS. Prefers the node's explicit T/R/S fields (lossless);
	// falls back to decomposing an explicit node matrix (rare for joint nodes).
	static JointTransform nodeLocalTRS(const tinygltf::Node& n)
	{
		JointTransform t;
		if (n.matrix.size() == 16)
		{
			glm::mat4 m;
			for (int c = 0; c < 4; c++)
				for (int r = 0; r < 4; r++)
					m[c][r] = static_cast<float>(n.matrix[c * 4 + r]);
			t.translation = glm::vec3(m[3]);
			glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
			t.scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
			glm::mat3 rot(
				t.scale.x > 0.0f ? c0 / t.scale.x : c0,
				t.scale.y > 0.0f ? c1 / t.scale.y : c1,
				t.scale.z > 0.0f ? c2 / t.scale.z : c2);
			t.rotation = glm::quat_cast(rot);
			return t;
		}
		if (n.translation.size() == 3)
			t.translation = glm::vec3(n.translation[0], n.translation[1], n.translation[2]);
		if (n.rotation.size() == 4) // glTF stores (x,y,z,w); glm::quat ctor takes (w,x,y,z)
			t.rotation = glm::quat(
				static_cast<float>(n.rotation[3]), static_cast<float>(n.rotation[0]),
				static_cast<float>(n.rotation[1]), static_cast<float>(n.rotation[2]));
		if (n.scale.size() == 3)
			t.scale = glm::vec3(n.scale[0], n.scale[1], n.scale[2]);
		return t;
	}

	void GLTFModelLoader::parseSkin(const tinygltf::Model& model)
	{
		if (model.skins.empty()) return;
		const tinygltf::Skin& skin = model.skins[0];
		const size_t jointCount = skin.joints.size();
		if (jointCount == 0) return;
		if (jointCount > 256)
			printf("[GLTF] skin has %zu joints (>256); per-vertex joint indices are clamped to u8\n", jointCount);

		skeleton.inverseBind.assign(jointCount, glm::mat4(1.0f));
		skeleton.restPose.resize(jointCount);
		skeleton.parents.assign(jointCount, -1);
		skeleton.names.resize(jointCount);
		for (size_t j = 0; j < jointCount; j++)
			skeleton.names[j] = model.nodes[skin.joints[j]].name; // bone name = joint node name

		// glTF node index -> joint index (= position within skin.joints), the space the
		// per-vertex JOINTS_0 values and the palette use.
		std::unordered_map<int, int> nodeToJoint;
		for (size_t j = 0; j < jointCount; j++) nodeToJoint[skin.joints[j]] = static_cast<int>(j);

		// Inverse bind matrices (optional in glTF; identity when absent).
		if (skin.inverseBindMatrices >= 0)
		{
			const tinygltf::Accessor&   acc  = model.accessors[skin.inverseBindMatrices];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			const float* m = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
			for (size_t j = 0; j < jointCount && j < acc.count; j++)
				skeleton.inverseBind[j] = glm::make_mat4(m + j * 16);
		}

		// Rest-pose local transform (TRS) per joint.
		for (size_t j = 0; j < jointCount; j++)
			skeleton.restPose[j] = nodeLocalTRS(model.nodes[skin.joints[j]]);

		// Parent links: each child node's parent joint is the node that lists it as a child.
		for (size_t j = 0; j < jointCount; j++)
			for (int child : model.nodes[skin.joints[j]].children)
			{
				auto it = nodeToJoint.find(child);
				if (it != nodeToJoint.end()) skeleton.parents[it->second] = static_cast<int>(j);
			}

		printf("[GLTF] skin parsed: %zu joints\n", jointCount);
	}

	void GLTFModelLoader::parseAnimations(const tinygltf::Model& model)
	{
		if (model.animations.empty() || skeleton.empty() || model.skins.empty()) return;

		// glTF node index -> joint index, to retarget channels onto skin[0]'s joints.
		const tinygltf::Skin& skin = model.skins[0];
		std::unordered_map<int, int> nodeToJoint;
		for (size_t j = 0; j < skin.joints.size(); j++) nodeToJoint[skin.joints[j]] = static_cast<int>(j);

		// Read a FLOAT accessor as a flat float array (samplers are always FLOAT in glTF).
		auto floatPtr = [&](int accessor, size_t& count, int& comps) -> const float* {
			const tinygltf::Accessor&   acc  = model.accessors[accessor];
			const tinygltf::BufferView& view = model.bufferViews[acc.bufferView];
			count = acc.count;
			comps = tinygltf::GetNumComponentsInType(acc.type);
			return reinterpret_cast<const float*>(&model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset]);
		};

		for (const tinygltf::Animation& anim : model.animations)
		{
			AnimationClip clip;
			clip.name = anim.name;

			clip.samplers.reserve(anim.samplers.size());
			for (const tinygltf::AnimationSampler& s : anim.samplers)
			{
				AnimSampler as;
				as.interp = (s.interpolation == "STEP")        ? Interp::STEP
				          : (s.interpolation == "CUBICSPLINE") ? Interp::CUBICSPLINE
				                                               : Interp::LINEAR;

				size_t inCount = 0;  int inComps = 0;
				const float* in = floatPtr(s.input, inCount, inComps);
				as.times.assign(in, in + inCount);
				for (float tt : as.times) clip.duration = std::max(clip.duration, tt);

				size_t outCount = 0; int outComps = 0;
				const float* out = floatPtr(s.output, outCount, outComps);
				as.values.resize(outCount);
				for (size_t k = 0; k < outCount; k++)
				{
					glm::vec4 v(0.0f);
					for (int c = 0; c < outComps && c < 4; c++) v[c] = out[k * outComps + c];
					as.values[k] = v;
				}
				clip.samplers.push_back(std::move(as));
			}

			for (const tinygltf::AnimationChannel& ch : anim.channels)
			{
				auto it = nodeToJoint.find(ch.target_node);
				if (it == nodeToJoint.end()) continue; // channel targets a non-joint node — skip
				AnimChannel ac;
				ac.joint   = it->second;
				ac.sampler = ch.sampler;
				if      (ch.target_path == "translation") ac.path = AnimPath::TRANSLATION;
				else if (ch.target_path == "rotation")    ac.path = AnimPath::ROTATION;
				else if (ch.target_path == "scale")       ac.path = AnimPath::SCALE;
				else continue; // morph-target weights unsupported
				clip.channels.push_back(ac);
			}

			animations.push_back(std::move(clip));
		}

		printf("[GLTF] parsed %zu animation clip(s)\n", animations.size());
	}

	void GLTFModelLoader::loadGLTFModel(const char* filename, bool mergeSolidSubmeshes)
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
		// .glb is packed binary (JSON + buffers + textures in one file); .gltf is text.
		const std::string path = util::enginePath(filename);
		const bool isBinary = path.size() >= 4 &&
			(path.compare(path.size() - 4, 4, ".glb") == 0 || path.compare(path.size() - 4, 4, ".GLB") == 0);
		bool ret = isBinary
			? loader.LoadBinaryFromFile(&model, &err, &warn, path)
			: loader.LoadASCIIFromFile(&model, &err, &warn, path);

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

		// Untextured materials carry only a solid baseColorFactor (linear RGBA). Bake it into a
		// 1x1 UNORM albedo texture so the existing albedo path shows the color. Deduped by value.
		auto solidColorAlbedo = [&](const std::vector<double>& bcf) -> uint16_t {
			if (!pTextureLoader) return 0;
			auto u8 = [](double v) -> uint8_t { return v <= 0.0 ? 0 : v >= 1.0 ? 255 : static_cast<uint8_t>(v * 255.0 + 0.5); };
			uint8_t px[4] = {
				bcf.size() > 0 ? u8(bcf[0]) : 255, bcf.size() > 1 ? u8(bcf[1]) : 255,
				bcf.size() > 2 ? u8(bcf[2]) : 255, bcf.size() > 3 ? u8(bcf[3]) : 255 };
			char name[32];
			snprintf(name, sizeof(name), "#solid%02x%02x%02x%02x", px[0], px[1], px[2], px[3]);
			return static_cast<uint16_t>(pTextureLoader->loadTextureFromMemory(name, px, 1, 1, VK_FORMAT_R8G8B8A8_UNORM));
		};

		// Build material table
		materials.resize(model.materials.size());
		for (size_t i = 0; i < model.materials.size(); i++)
		{
			const auto& mat = model.materials[i];
			const auto& pbr = mat.pbrMetallicRoughness;
			// Albedo/emission are sRGB color data; normal, metalRough, AO are linear data
			materials[i].albedoMap     = (pbr.baseColorTexture.index >= 0)
				? tryLoadGLTFTexture(model, modelDir, pbr.baseColorTexture.index, VK_FORMAT_R8G8B8A8_SRGB)
				: solidColorAlbedo(pbr.baseColorFactor);
			materials[i].normalMap     = tryLoadGLTFTexture(model, modelDir, mat.normalTexture.index,             VK_FORMAT_R8G8B8A8_UNORM);
			materials[i].metalRoughMap = tryLoadGLTFTexture(model, modelDir, pbr.metallicRoughnessTexture.index, VK_FORMAT_R8G8B8A8_UNORM);
			materials[i].emissionMap   = tryLoadGLTFTexture(model, modelDir, mat.emissiveTexture.index,           VK_FORMAT_R8G8B8A8_SRGB);
		}

		// Some glTFs (e.g. quick Blender exports) carry geometry but declare no materials, so
		// every primitive references material -1. Without a material slot the vertices fall back
		// to slot 0 = the all-unused entry, which the shader renders as the bare vertex-color/grid.
		// Synthesize one default material (neutral mid-gray albedo) so such models shade normally.
		if (materials.empty())
		{
			std::cout << "\tno materials in file — applying default material\n";
			BGLModel::Material def{};
			def.albedoMap = solidColorAlbedo({ 0.8, 0.8, 0.8, 1.0 });
			materials.push_back(def);
		}
		// Build this model's skin block before building vertices (reads the "<model>.yaml"
		// sidecar), so each vertex can store its local material slot (see buildPrimitiveVertices).
		buildSkinBlock(filename);

		auto primIsTransparent = [&](const tinygltf::Primitive& prim) -> bool {
			return prim.material >= 0
			    && prim.material < static_cast<int>(model.materials.size())
			    && isTransparent(model.materials[prim.material]);
		};

		// Pass 1: solid/opaque primitives.
		if (mergeSolidSubmeshes)
		{
			// Merge all opaque primitives across all meshes into submesh 0.
			SubmeshInfo opaqueSM{};
			opaqueSM.firstIndex  = 0;
			opaqueSM.firstVertex = 0;
			opaqueSM.transparentMaterial = false;

			for (const tinygltf::Mesh& mesh : model.meshes)
				for (const tinygltf::Primitive& prim : mesh.primitives)
					if (!primIsTransparent(prim))
						appendPrimitive(model, prim);

			opaqueSM.indexCount  = static_cast<uint32_t>(indices.size());
			opaqueSM.vertexCount = static_cast<uint32_t>(vertices.size());
			submeshes.push_back(opaqueSM);
		}
		else
		{
			// Keep each opaque primitive as its own submesh. glTF files often pack the
			// whole model into a single mesh with many primitives (e.g. Sponza is one
			// mesh of 103 primitives), so splitting per primitive — not per mesh — is
			// what actually yields multiple submeshes for per-submesh frustum culling.
			for (const tinygltf::Mesh& mesh : model.meshes)
				for (const tinygltf::Primitive& prim : mesh.primitives)
				{
					if (primIsTransparent(prim)) continue;

					SubmeshInfo osm{};
					osm.firstIndex  = static_cast<uint32_t>(indices.size());
					osm.firstVertex = static_cast<uint32_t>(vertices.size());
					osm.transparentMaterial = false;

					appendPrimitive(model, prim);

					osm.indexCount  = static_cast<uint32_t>(indices.size()) - osm.firstIndex;
					osm.vertexCount = static_cast<uint32_t>(vertices.size()) - osm.firstVertex;
					submeshes.push_back(osm);
				}
		}

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

		// Skeleton (joints, inverse-bind, hierarchy, rest pose). Independent of the per-vertex
		// data above; the baker consumes it to build the joint-matrix palette.
		parseSkin(model);
		parseAnimations(model);
		// Backfill any trailing non-skinned primitives so skinInfluences stays index-parallel
		// to `vertices` (resize only grows, default-filling weight 1 on joint 0).
		if (!skinInfluences.empty()) skinInfluences.resize(vertices.size());

		if (!tangentsLoaded)
			printf("[GLTF] No TANGENT attribute found — tangents will be computed\n");
	}
}
