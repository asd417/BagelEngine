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
    void OBJModelLoader::load(ModelLoadSettings parms){
        const char *filename = parms.source.c_str();
        std::string fullPath = util::enginePath(filename);
		std::string materialPath = util::enginePath("/models");
        tinyobj::ObjReaderConfig reader_config;
        reader_config.mtl_search_path = materialPath; // Path to material files
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
        // just put every primitives in a single submesh. it doesnt matter for obj
        submeshes.resize(1);
        materials.resize(materialCount);
        for (int i = 0; i < materialCount; i++)
        {
            materials[i].albedoMap   = tryLoadTexture(pTextureLoader, materialPath, m[i].diffuse_texname);
            materials[i].emissionMap = tryLoadTexture(pTextureLoader, materialPath, m[i].emissive_texname);
            materials[i].normalMap   = tryLoadTexture(pTextureLoader, materialPath, m[i].normal_texname);
            const std::string& roughName = m[i].roughness_texname;
            const std::string& metalName = m[i].metallic_texname;
            if (!roughName.empty() && !metalName.empty() && pTextureLoader) {
                // Both exist: bake a combined ORM texture (G=roughness, B=metallic)
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
		auto &attrib = reader.GetAttrib();
		auto &shapes = reader.GetShapes();

		std::unordered_map<BGLModel::Vertex, uint32_t, BGLModel::VertexHasher, BGLModel::VertexEquals> vertexMap{};
		uint32_t vertInt = 0;
		if (parms.buildMode == ComponentBuildMode::FACES)
		{
			uint32_t faceID = 0;
			for (size_t s = 0; s < shapes.size(); s++)
			{

				size_t index_offset = 0;
				for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
				{
					size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

					// per-face material
					uint32_t materialID = shapes[s].mesh.material_ids[f];

					// Loop over vertices in the face.
					for (size_t v = 0; v < fv; v++)
					{
						BGLModel::Vertex vertex{};
						// access to vertex
						tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
						tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
						tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
						tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];
						vertex.position = {vx, vy, vz};

						// Check if `normal_index` is zero or positive. negative = no normal data
						if (idx.normal_index >= 0)
						{
							tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
							tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
							tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];
							vertex.normal = {nx, ny, nz};
						}

						// Check if `texcoord_index` is zero or positive. negative = no texcoord data
						if (idx.texcoord_index >= 0)
						{
							tinyobj::real_t tx = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
							tinyobj::real_t ty = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
							vertex.uv = {tx, ty};
						}

						// Optional: vertex colors
						tinyobj::real_t red = attrib.colors[3 * size_t(idx.vertex_index) + 0];
						tinyobj::real_t green = attrib.colors[3 * size_t(idx.vertex_index) + 1];
						tinyobj::real_t blue = attrib.colors[3 * size_t(idx.vertex_index) + 2];
						vertex.color = {red, green, blue};

						if (materialID >= 0 && materialID < static_cast<int>(materials.size()))
						{
							const BGLModel::Material &mat = materials[materialID];
							vertex.albedoMap    = mat.albedoMap;
							vertex.aoMap        = mat.aoMap;
							vertex.emissionMap  = mat.emissionMap;
							vertex.heightMap    = mat.heightMap;
							vertex.normalMap     = mat.normalMap;
							vertex.metalRoughMap = mat.metalRoughMap;
							vertex.specularMap   = mat.specularMap;
							vertex.opacityMap    = mat.opacityMap;
							vertex.refractionMap = mat.refractionMap;
						}

						int index;
						if (auto search = vertexMap.find(vertex); search != vertexMap.end())
						{
							// vertex already exists in map
							index = search->second;
						}
						else
						{
							// new vertex
							vertexMap.emplace(vertex, vertInt);
							index = vertInt;
							vertInt++;
							vertices.push_back(vertex);
						}
						// vertices.push_back(vertex);
						indices.push_back(index);
					}
					index_offset += fv;
				}
			}
		}
		else
		{
			// When loading model as wireframe
			// Loading model as wireframe means it does not need material set configured
			for (const auto &shape : shapes)
			{
				for (const auto &index : shape.lines.indices)
				{
					BGLModel::Vertex vertex{};
					if (index.vertex_index >= 0)
					{
						vertex.position = {attrib.vertices[3 * index.vertex_index + 0], attrib.vertices[3 * index.vertex_index + 1], attrib.vertices[3 * index.vertex_index + 2]};
					}

					vertex.color = {attrib.colors[3 * index.vertex_index], attrib.colors[3 * index.vertex_index + 1], attrib.colors[3 * index.vertex_index + 2]};

					if (index.normal_index >= 0)
					{
						vertex.normal = {attrib.normals[3 * index.normal_index + 0], attrib.normals[3 * index.normal_index + 1], attrib.normals[3 * index.normal_index + 2]};
					}
					if (index.texcoord_index >= 0)
					{
						vertex.uv = {attrib.texcoords[2 * index.texcoord_index + 0], 1 - attrib.texcoords[2 * index.texcoord_index + 1]};
					}
					int vertexIndex;
					if (auto search = vertexMap.find(vertex); search != vertexMap.end())
					{
						// vertex already exists in map
						vertexIndex = search->second;
					}
					else
					{
						// new vertex
						vertexMap.emplace(vertex, vertInt);
						vertexIndex = vertInt;
						vertInt++;
						vertices.push_back(vertex);
					}
					// vertices.push_back(vertex);
					indices.push_back(vertexIndex);
				}
			}
		}
		std::cout << "Model Loader looped through " << vertInt << "\n";
		std::cout << "Vertex Buffer size " << vertices.size() << "\n";
		std::cout << "Index Buffer size " << indices.size() << "\n";
		submeshes[0].firstIndex  = 0;
		submeshes[0].indexCount  = static_cast<uint32_t>(indices.size());
		submeshes[0].firstVertex = 0;
		submeshes[0].vertexCount = static_cast<uint32_t>(vertices.size());
	}
}