#include "model/model_component_builder.hpp"
#include "model_loaders/generated.hpp"
#include "model_loaders/gltf.hpp"
#include "model_loaders/obj.hpp"

// vulkan headers
#include <stdexcept>
#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <unordered_map>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "engine/bagel_engine_device.hpp"

// lib
#include "imgui/bagel_imgui.hpp"
#include "model/bagel_model.hpp"

#define CONSOLE ConsoleApp::Instance()

namespace bagel
{

std::ostream &operator<<(std::ostream &os, const BGLModel::Vertex &other)
{
    return os << "P " << other.position.x << "\t" << other.position.y << "\t" << other.position.z << "\tN " << other.normal.x << "\t" << other.normal.y << "\t" << other.normal.z << " C " << other.color.x << "\t" << other.color.y << "\t" << other.color.z << "\n";
}

std::vector<VkVertexInputBindingDescription> BGLModel::Vertex::getBindingDescriptions()
{
    // Possible to write this in brace construction {{0, sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}}
    std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex); // This allows easier addition of attribute as the stride will automatically adjusted.
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescriptions;
}

std::vector<VkVertexInputAttributeDescription> BGLModel::Vertex::getAttributeDescriptions()
{
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

    // We want both color and vertex in one binding so the binding is kept 0
    attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)}); // location, binding, format, offset
    attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)});
    attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
    attributeDescriptions.push_back({3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)});
    attributeDescriptions.push_back({4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});
    attributeDescriptions.push_back({5, 0, VK_FORMAT_R16_UINT, offsetof(Vertex, materialIndex)});
    // offsetof macro calculates the byte offset of color member in the Vertex struct

    return attributeDescriptions;
}

// Builder class of all Model-loading components
// Any component with following members is valid:
//  std::string modelName;
//  uint32_t vertexCount;
//  uint32_t indexCount;

ModelComponentBuilder::ModelComponentBuilder(
    BGLDevice &_bglDevice,
    entt::registry &_r) : bglDevice{_bglDevice},
                          registry{_r}
{
}
void ModelComponentBuilder::configureModelMaterialSet(std::vector<GLTFMaterial> *set)
{
    if (p_materialSet == nullptr)
        p_materialSet = set;
    else
        std::cout << "ModelComponentBuilder::configureModelMaterialSet() Could not configure material set, remove existing material set first.";
}

void ModelComponentBuilder::loadModel(const char *filename, ModelLoadSettings buildSettings)
{
    // Intercept well-known names as procedural geometry
    const char *genName = nullptr;
    if (strcmp(filename, "grid") == 0)
        genName = "grid";
    else if (strcmp(filename, "/models/cube.obj") == 0)
        genName = "cube";
    else if (strcmp(filename, "/models/floor.obj") == 0)
        genName = "floor";
    else if (strcmp(filename, "/models/sphere.obj") == 0)
        genName = "sphere";
    else if (strcmp(filename, "/models/icosphere.obj") == 0)
        genName = "icosphere";
    else if (strcmp(filename, "/models/wirecube.obj") == 0)
        genName = "wirecube";
    else if (strcmp(filename, "/models/wiresphere.obj") == 0)
        genName = "wiresphere";
    else if (strcmp(filename, "/models/axis.obj") == 0)
        genName = "axis";
    // NOTE: "planet" is intentionally NOT handled here. Planet geometry is procedural and is
    // built directly by PlanetComponentSystem (createPlanet / rebuildPlanetMesh), never loaded
    // as a model asset, so it never reaches this loader.

    if (genName)
    {
        buildSettings.source = genName;
        activeLoader = std::make_unique<GeneratedModelLoader>();
    }
    else
    {
        buildSettings.source = filename;
        const char *ext = strrchr(filename, '.');
        if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
        {
            activeLoader = std::make_unique<GLTFModelLoader>(pTextureLoader);
        }
        else if (ext && strcmp(ext, ".obj") == 0)
        {
            activeLoader = std::make_unique<OBJModelLoader>(pTextureLoader);
        }
        else
        {
            // Any other extension is delegated to a derived builder's factory (e.g. LEGO's
            // ".dat" -> LDrawModelLoader). Base returns nullptr => genuinely unknown.
            activeLoader = createLoaderForExtension(ext ? ext : "");
            if (!activeLoader)
            {
                std::cerr << "[ModelComponentBuilder] Unknown file type: " << filename << "\n";
                return;
            }
        }
    }
    activeLoader->setMaterialManager(pMaterialManager);
    activeLoader->load(buildSettings);
}

// Resolve `modelFileName` to a shared, cache-owned Model (built once per source) and
// attach a ModelComponent that references it. Deduplication is now just a cache lookup:
// two entities using the same source point at ONE Model, and neither owns its buffers, so
// destroying either entity frees nothing GPU-side (no more owner/borrower double-free).
ModelComponent &ModelComponentBuilder::buildComponent(entt::entity targetEnt, const char *modelFileName, ModelLoadSettings buildSettings)
{
    ModelComponent &comp = registry.emplace<ModelComponent>(targetEnt);
    comp.loadSettings = buildSettings;
    comp.loadSettings.source = modelFileName; // source path = the model's identity / cache key

    ModelCacheManager &cache = ModelCacheManager::get();
    const std::string key = modelFileName;

    if (Model *cached = cache.find(key))
    {
        // Cache hit: share the existing geometry. Non-skinned instances are fully
        // described by the shared Model, so there's nothing else to do.
        comp.model = cached;
        if (!cached->isSkinned)
            return comp;
        // Skinned duplicate (rare — skinned models are usually single-instance): the
        // influences/geometry are shared, but each entity needs its own AnimationComponent.
        // Re-parse the source for skeleton/clips (no buffers created) and bake playback state.
        loadModel(modelFileName, buildSettings);
        attachSkinningState(targetEnt);
        activeLoader.reset();
        return comp;
    }

    Model &model = cache.create(key);
    model.loadSettings = comp.loadSettings;
    comp.model = &model;

    loadModel(modelFileName, buildSettings);

    // Not supported in lines mode. There is no concept of face in lines mode and vertex array can be smaller than 3
    if (buildSettings.buildMode != LINES && !activeLoader->hasTangents())
        activeLoader->calculateTangent();

    auto &vertices = activeLoader->getVertices();
    auto &indices = activeLoader->getIndices();
    auto &submeshes = activeLoader->getSubmeshes();

    computeModelBounds(model, vertices);

    std::cout << "Creating Vertex Buffer\n";
    // the assumption is that if the faces can be regenerated, the vertices will usually be regenerated as well
    model.mappedVB = createVertexBuffer(sizeof(BGLModel::Vertex) * vertices.size(), model.vertexBuffer, model.vertexMemory, buildSettings.isDeformable || buildSettings.isDynamic);
    if (indices.size() > 0)
    {
        std::cout << "Model has Index Buffer. Allocating...\n";
        model.mappedIB = createIndexBuffer(sizeof(uint32_t) * indices.size(), model.indexBuffer, model.indexMemory, buildSettings.isDynamic);
    }
    populateSubmeshes(model, submeshes, vertices);
    model.indexCount = static_cast<uint32_t>(indices.size());
    model.vertexCount = static_cast<uint32_t>(vertices.size());
    // Skin block the loader reserved/filled for this model (numSkins from the sidecar).
    model.skinBase = static_cast<uint16_t>(activeLoader->getSkinBase());
    model.numSlots = static_cast<uint16_t>(activeLoader->getNumSlots());
    model.numSkins = static_cast<uint8_t>(activeLoader->getNumSkins());

    // Skeletal skinning: upload per-vertex influences ONCE for the shared model, then bake
    // this entity's animation/playback state (a per-entity AnimationComponent).
    if (pSkinManager && activeLoader->isSkinned())
    {
        auto &infl = activeLoader->getSkinInfluences();
        model.skinVertexBase = pSkinManager->uploadInfluences(infl.data(), static_cast<uint32_t>(infl.size()));
        model.isSkinned = true;
        attachSkinningState(targetEnt);
    }
    activeLoader.reset();
    std::cout << "Finished building Component\n";
    return comp;
}

void ModelComponentBuilder::computeModelBounds(Model &model, const std::vector<BGLModel::Vertex> &verts)
{
    glm::vec3 bMin(std::numeric_limits<float>::max());
    glm::vec3 bMax(-std::numeric_limits<float>::max());
    for (const auto &v : verts)
    {
        bMin = glm::min(bMin, v.position);
        bMax = glm::max(bMax, v.position);
    }
    model.aabbMin = bMin;
    model.aabbMax = bMax;
}

void ModelComponentBuilder::populateSubmeshes(Model &model, const std::vector<SubmeshInfo> &submeshes, const std::vector<BGLModel::Vertex> &verts)
{
    // Rebuild from scratch so this serves both initial builds and in-place edits (counts may shrink).
    model.submeshCount = 0;
    model.solidSubmeshCount = 0;
    bool seenTransparent = false;
    for (const auto &smi : submeshes)
    {
        assert(model.submeshCount < Model::MAX_SUBMESHES && "Exceeded MAX_SUBMESHES");
        // Loaders emit all solid submeshes before any transparent ones; track the split so
        // the model can filter by transparency with a single index.
        if (smi.transparentMaterial)
        {
            seenTransparent = true;
        }
        else
        {
            assert(!seenTransparent && "Submeshes must be ordered solid-first, then transparent");
            model.solidSubmeshCount++;
        }
        Model::Submesh &sm = model.submeshes[model.submeshCount++];
        sm.firstIndex = smi.firstIndex;
        sm.indexCount = smi.indexCount;
        sm.firstVertex = smi.firstVertex;
        sm.vertexCount = smi.vertexCount;
        sm.materialIndex = smi.materialIndex;
        // Per-submesh AABB in model space.
        glm::vec3 sMin(std::numeric_limits<float>::max());
        glm::vec3 sMax(-std::numeric_limits<float>::max());
        const uint32_t vEnd = smi.firstVertex + smi.vertexCount;
        for (uint32_t vi = smi.firstVertex; vi < vEnd; vi++)
        {
            sMin = glm::min(sMin, verts[vi].position);
            sMax = glm::max(sMax, verts[vi].position);
        }
        sm.aabbMin = sMin;
        sm.aabbMax = sMax;
    }
}

} // namespace bagel