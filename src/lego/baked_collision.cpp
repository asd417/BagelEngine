#include "baked_collision.hpp"

// TINYGLTF_IMPLEMENTATION is owned by src/model_loaders/gltf.cpp; here we only use the
// declarations to parse the small collision GLBs (positions only, no images/materials).
// The NO_STB_IMAGE* macros must match that implementation TU's configuration, else the
// TinyGLTF ctor in this TU pulls in image callbacks that gltf.cpp compiled out (LNK2019).
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace bagel::ldraw {

	namespace {
		namespace fs = std::filesystem;

		// Same normalization the connector cache uses: strip directory + ".dat", lowercase.
		// The baker names each file after the part stem, so "3001", "3001.dat" and
		// "s/3001.dat" all resolve to "3001.glb".
		std::string keyOf(std::string s) {
			std::replace(s.begin(), s.end(), '\\', '/');
			auto slash = s.find_last_of('/');
			if (slash != std::string::npos) s = s.substr(slash + 1);
			if (s.size() >= 4) {
				std::string ext = s.substr(s.size() - 4);
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext == ".dat") s.resize(s.size() - 4);
			}
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// Pull a primitive's POSITION accessor into a vec3 list (raw LDU). Assumes float
		// VEC3 (what the baker writes); anything else yields an empty hull and is skipped.
		std::vector<glm::vec3> readPositions(const tinygltf::Model& m, const tinygltf::Primitive& prim) {
			std::vector<glm::vec3> out;
			auto it = prim.attributes.find("POSITION");
			if (it == prim.attributes.end()) return out;
			const tinygltf::Accessor& acc = m.accessors[it->second];
			if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || acc.type != TINYGLTF_TYPE_VEC3)
				return out;
			const tinygltf::BufferView& bv = m.bufferViews[acc.bufferView];
			const tinygltf::Buffer& buf = m.buffers[bv.buffer];
			const size_t stride = acc.ByteStride(bv);
			const unsigned char* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
			out.reserve(acc.count);
			for (size_t i = 0; i < acc.count; ++i) {
				float v[3];
				std::memcpy(v, base + i * stride, sizeof(v));
				out.push_back({ v[0], v[1], v[2] });
			}
			return out;
		}
	}

	const std::vector<std::vector<glm::vec3>>* BakedCollision::find(const std::string& partName) {
		const std::string key = keyOf(partName);
		if (auto it = cache_.find(key); it != cache_.end()) return &it->second;
		if (missing_.count(key)) return nullptr;

		const std::string path = (fs::path(dir_) / (key + ".glb")).string();
		if (!fs::exists(path)) { missing_.insert(key); return nullptr; }

		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err, warn;
		if (!loader.LoadBinaryFromFile(&model, &err, &warn, path)) {
			std::cerr << "[BakedCollision] parse failed: " << path << " : " << err << "\n";
			missing_.insert(key);
			return nullptr;
		}

		std::vector<std::vector<glm::vec3>> hulls;   // one per primitive across all meshes
		for (const tinygltf::Mesh& mesh : model.meshes)
			for (const tinygltf::Primitive& prim : mesh.primitives) {
				std::vector<glm::vec3> pts = readPositions(model, prim);
				if (!pts.empty()) hulls.push_back(std::move(pts));
			}
		if (hulls.empty()) { missing_.insert(key); return nullptr; }

		auto [it, _] = cache_.emplace(key, std::move(hulls));
		return &it->second;
	}

} // namespace bagel::ldraw
