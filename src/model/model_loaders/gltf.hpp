#pragma once
#include "bagel_model_loader.hpp"
#include "tiny_gltf.h"
namespace bagel 
{
    class GLTFModelLoader : public ModelLoaderBase 
    {
        public:
        GLTFModelLoader(BGLTextureLoader* pTL);
        ~GLTFModelLoader() = default;
        void load(ModelLoadSettings buildSettings) override;

        // Returns true if the material requires alpha blending or has cut-out transparency.
        static bool isTransparent(const tinygltf::Material& mat);

        private:
        void loadGLTFModel(const char *filename, bool mergeSolidSubmeshes);
        void appendPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& prim);
        // Parse model.skins[0] into `skeleton` (joints, inverse-bind, hierarchy, rest pose).
        // No-op if the model has no skin. Call after meshes are loaded.
        void parseSkin(const tinygltf::Model& model);
        // Parse model.animations into `animations` (samplers + channels retargeted to joints).
        // No-op if there is no skin or no animation. Call after parseSkin.
        void parseAnimations(const tinygltf::Model& model);
        uint16_t tryLoadGLTFTexture(const tinygltf::Model& model, const std::string& modelDir, int texIdx, VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB);
    };
}