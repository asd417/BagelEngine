#pragma once
#include "../bagel_model_loader.hpp"
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
        uint16_t tryLoadGLTFTexture(const tinygltf::Model& model, const std::string& modelDir, int texIdx, VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB);
    };
}