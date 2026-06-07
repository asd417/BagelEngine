#pragma once
#include "../bagel_model_loader.hpp"
namespace bagel {
    class GeneratedModelLoader : public ModelLoaderBase 
    {
        public:
        GeneratedModelLoader(BGLTextureLoader* pTL = nullptr);
        ~GeneratedModelLoader() = default;
        void load(ModelLoadSettings buildSettings) override;
        private:
        void generateGrid(ModelLoadSettings buildSettings);
        void generateCube(ModelLoadSettings buildSettings);
        void generateFloor(ModelLoadSettings buildSettings);
        void generateSphere(ModelLoadSettings buildSettings);
        void generateIcosphere(ModelLoadSettings buildSettings);
        void generateWireCube(ModelLoadSettings buildSettings);
        void generateWireSphere(ModelLoadSettings buildSettings);
        void generateAxis(ModelLoadSettings buildSettings);
    };
}