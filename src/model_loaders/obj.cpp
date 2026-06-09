#define TINYOBJLOADER_IMPLEMENTATION
#include "obj.hpp"
#include <cassert>
#include <filesystem>
#include "../bagel_util.hpp"

namespace bagel {
    OBJModelLoader::OBJModelLoader(BGLTextureLoader* pTL) : ModelLoaderBase(pTL)
	{};

    inline uint16_t tryLoadTexture(BGLTextureLoader* tl, const std::string& materialPath, const std::string& texName){
        if (texName.empty() || !tl) return 0;
        std::string fullTexPath = materialPath + "/" + texName;
        if (!std::filesystem::exists(fullTexPath)) {
            std::cerr << "[OBJModelLoader] texture not found: " << fullTexPath << "\n";
            return 0;
        }
        return static_cast<uint16_t>(tl->loadTexture(texName.c_str()));
    };

    bool OBJModelLoader::isTransparent(const tinyobj::material_t& mat)
    {
        if (mat.dissolve < 1.0f)           return true;
        if (!mat.alpha_texname.empty())    return true;
        return false;
    }

    void OBJModelLoader::load(ModelLoadSettings parms){
        const char *filename = parms.source.c_str();
        std::string fullPath = util::enginePath(filename);
		std::string materialPath = util::enginePath("/models");
        tinyobj::ObjReaderConfig reader_config;
        reader_config.mtl_search_path = materialPath;
        tinyobj::ObjReader reader;

        if (!reader.ParseFromFile(fullPath, reader_config))
		{
			if (!reader.Error().empty())
			{
				std::cerr << "TinyObjReader: " << reader.Error();
			}
			throw std::exception();
		}
        if (!reader.Warning().empty())
		{
			std::cout << "TinyObjReader: " << reader.Warning();
		}
        const auto& m = reader.GetMaterials();
        int materialCount = m.size();
        materials.resize(materialCount);
        for (int i = 0; i < materialCount; i++)
        {
            materials[i].albedoMap   = tryLoadTexture(pTextureLoader, materialPath, m[i].diffuse_texname);
            materials[i].emissionMap = tryLoadTexture(pTextureLoader, materialPath, m[i].emissive_texname);
            materials[i].normalMap   = tryLoadTexture(pTextureLoader, materialPath, m[i].normal_texname);
            const std::string& roughName = m[i].roughness_texname;
            const std::string& metalName = m[i].metallic_texname;
            if (!roughName.empty() && !metalName.empty() && pTextureLoader) {
                std::string roughRel = "/models/" + roughName;
                std::string metalRel = "/models/" + metalName;
                materials[i].metalRoughMap = static_cast<uint16_t>(
                    pTextureLoader->loadCombinedMetalRough(roughRel.c_str(), metalRel.c_str()));
            } else {
                materials[i].metalRoughMap = tryLoadTexture(pTextureLoader, materialPath, roughName);
            }
        }
        loadOBJModel(reader, parms);
    }

    void OBJModelLoader::loadOBJModel(tinyobj::ObjReader& reader, ModelLoadSettings parms)
	{
		auto& attrib = reader.GetAttrib();
		auto& shapes = reader.GetShapes();
		const auto& m = reader.GetMaterials();

		if (parms.buildMode == ComponentBuildMode::FACES)
		{
			using VertexMap = std::unordered_map<BGLModel::Vertex, uint32_t, BGLModel::VertexHasher, BGLModel::VertexEquals>;

			// Appends one face (fv vertices starting at index_offset) into the given vertex map.
			// Vertices are deduplicated within vmap; new vertices are appended to this->vertices.
			auto appendFace = [&](const tinyobj::shape_t& shape, size_t f, size_t index_offset,
			                      size_t fv, uint32_t materialID, VertexMap& vmap, uint32_t& vint)
			{
				for (size_t v = 0; v < fv; v++)
				{
					BGLModel::Vertex vertex{};
					tinyobj::index_t idx = shape.mesh.indices[index_offset + v];

					vertex.position = {
						attrib.vertices[3 * size_t(idx.vertex_index) + 0],
						attrib.vertices[3 * size_t(idx.vertex_index) + 1],
						attrib.vertices[3 * size_t(idx.vertex_index) + 2]
					};
					if (idx.normal_index >= 0)
						vertex.normal = {
							attrib.normals[3 * size_t(idx.normal_index) + 0],
							attrib.normals[3 * size_t(idx.normal_index) + 1],
							attrib.normals[3 * size_t(idx.normal_index) + 2]
						};
					if (idx.texcoord_index >= 0)
						vertex.uv = {
							attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
							attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]
						};
					vertex.color = {
						attrib.colors[3 * size_t(idx.vertex_index) + 0],
						attrib.colors[3 * size_t(idx.vertex_index) + 1],
						attrib.colors[3 * size_t(idx.vertex_index) + 2]
					};
					if (materialID < static_cast<uint32_t>(materials.size()))
					{
						const BGLModel::Material& mat = materials[materialID];
						vertex.albedoMap     = mat.albedoMap;
						vertex.aoMap         = mat.aoMap;
						vertex.emissionMap   = mat.emissionMap;
						vertex.heightMap     = mat.heightMap;
						vertex.normalMap     = mat.normalMap;
						vertex.metalRoughMap = mat.metalRoughMap;
						vertex.specularMap   = mat.specularMap;
						vertex.opacityMap    = mat.opacityMap;
						vertex.refractionMap = mat.refractionMap;
					}

					if (auto it = vmap.find(vertex); it != vmap.end())
						indices.push_back(it->second);
					else
					{
						vmap.emplace(vertex, vint);
						indices.push_back(vint);
						vint++;
						vertices.push_back(vertex);
					}
				}
			};

			auto matIsTransparent = [&](int matID) -> bool {
				return matID >= 0
				    && matID < static_cast<int>(m.size())
				    && isTransparent(m[matID]);
			};

			// Pass 1: merge all opaque faces across all shapes into submesh 0
			submeshes.push_back({});
			SubmeshInfo& opaqueSM = submeshes[0];
			opaqueSM.firstIndex  = 0;
			opaqueSM.firstVertex = 0;
			opaqueSM.transparentMaterial = false;

			VertexMap opaqueMap{};
			uint32_t opaqueVint = 0;
			for (const tinyobj::shape_t& shape : shapes)
			{
				size_t index_offset = 0;
				for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
				{
					size_t fv       = shape.mesh.num_face_vertices[f];
					int    materialID = shape.mesh.material_ids[f];
					if (!matIsTransparent(materialID))
						appendFace(shape, f, index_offset, fv, static_cast<uint32_t>(materialID), opaqueMap, opaqueVint);
					index_offset += fv;
				}
			}
			opaqueSM.indexCount  = static_cast<uint32_t>(indices.size());
			opaqueSM.vertexCount = static_cast<uint32_t>(vertices.size());

			// Pass 2: one submesh per shape for its transparent faces
			for (const tinyobj::shape_t& shape : shapes)
			{
				bool hasTransparent = false;
				for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
					if (matIsTransparent(shape.mesh.material_ids[f])) { hasTransparent = true; break; }
				if (!hasTransparent) continue;

				SubmeshInfo tsm{};
				tsm.firstIndex  = static_cast<uint32_t>(indices.size());
				tsm.firstVertex = static_cast<uint32_t>(vertices.size());
				tsm.transparentMaterial = true;

				VertexMap transparentMap{};
				uint32_t transparentVint = static_cast<uint32_t>(vertices.size());

				size_t index_offset = 0;
				for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
				{
					size_t fv       = shape.mesh.num_face_vertices[f];
					int    materialID = shape.mesh.material_ids[f];
					if (matIsTransparent(materialID))
						appendFace(shape, f, index_offset, fv, static_cast<uint32_t>(materialID), transparentMap, transparentVint);
					index_offset += fv;
				}

				tsm.indexCount  = static_cast<uint32_t>(indices.size()) - tsm.firstIndex;
				tsm.vertexCount = static_cast<uint32_t>(vertices.size()) - tsm.firstVertex;
				submeshes.push_back(tsm);
			}
		}
		else
		{
			// Lines mode: no material classification, single submesh
			submeshes.push_back({});
			std::unordered_map<BGLModel::Vertex, uint32_t, BGLModel::VertexHasher, BGLModel::VertexEquals> vertexMap{};
			uint32_t vertInt = 0;
			for (const auto& shape : shapes)
			{
				for (const auto& index : shape.lines.indices)
				{
					BGLModel::Vertex vertex{};
					if (index.vertex_index >= 0)
						vertex.position = { attrib.vertices[3 * index.vertex_index + 0], attrib.vertices[3 * index.vertex_index + 1], attrib.vertices[3 * index.vertex_index + 2] };
					vertex.color = { attrib.colors[3 * index.vertex_index], attrib.colors[3 * index.vertex_index + 1], attrib.colors[3 * index.vertex_index + 2] };
					if (index.normal_index >= 0)
						vertex.normal = { attrib.normals[3 * index.normal_index + 0], attrib.normals[3 * index.normal_index + 1], attrib.normals[3 * index.normal_index + 2] };
					if (index.texcoord_index >= 0)
						vertex.uv = { attrib.texcoords[2 * index.texcoord_index + 0], 1 - attrib.texcoords[2 * index.texcoord_index + 1] };

					if (auto it = vertexMap.find(vertex); it != vertexMap.end())
						indices.push_back(it->second);
					else
					{
						vertexMap.emplace(vertex, vertInt);
						indices.push_back(vertInt);
						vertInt++;
						vertices.push_back(vertex);
					}
				}
			}
			submeshes[0].firstIndex  = 0;
			submeshes[0].indexCount  = static_cast<uint32_t>(indices.size());
			submeshes[0].firstVertex = 0;
			submeshes[0].vertexCount = static_cast<uint32_t>(vertices.size());
		}

		std::cout << "Model Loader looped through " << vertices.size() << " unique vertices\n";
		std::cout << "Vertex Buffer size " << vertices.size() << "\n";
		std::cout << "Index Buffer size " << indices.size() << "\n";
	}
}
