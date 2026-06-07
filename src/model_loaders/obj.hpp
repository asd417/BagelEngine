#pragma once
#include "../bagel_model_loader.hpp"
#include <tiny_obj_loader.h>
namespace bagel
{
    class OBJModelLoader : public ModelLoaderBase
    {
    public:
        OBJModelLoader(BGLTextureLoader* pTL);
        ~OBJModelLoader() = default;
        void load(ModelLoadSettings parms) override;
    private:
        void loadOBJModel(tinyobj::ObjReader& reader, ModelLoadSettings parms);
    };
}