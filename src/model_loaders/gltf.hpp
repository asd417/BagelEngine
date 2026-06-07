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
        private:
        void loadGLTFModel(const char *filename, uint32_t maxPrimitives);
        void loadGLTFMesh(const tinygltf::Model& model, const tinygltf::Mesh& mesh, uint32_t maxPrimitives);
    };
}