#include "bagel_model_loader.hpp"
#include "bagel_material.hpp"
#include "model_sidecar.hpp"
// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <time.h>
#include <cstdio>
#include <cstdarg>


namespace bagel
{
	class Stopwatch
	{
	public:
		Stopwatch() { reset(); }
		void reset() { m_start = clock(); }
		double elapsed() const { return (clock() - m_start) * 1000.0 / CLOCKS_PER_SEC; }

	private:
		clock_t m_start;
	};

	static int xatlasPrint(const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		char buf[1024];
		vsnprintf(buf, sizeof(buf), format, args); // safe, bounded
		va_end(args);
		// forward `buf` to your logging system here
		return (int)strlen(buf);
	}

	void ModelLoaderBase::buildSkinBlock(const std::string& modelSourcePath)
	{
		numSlots = materials.empty() ? 1u : static_cast<uint32_t>(materials.size());

		const ModelSidecar sidecar = ModelSidecar::loadForModel(modelSourcePath);
		numSkins = sidecar.numSkins();
		if (numSkins > 255) numSkins = 255; // ModelComponent::skinIndex / numSkins are uint8

		if (!pMaterialManager) { skinBase = 0; return; }

		// Block is numSkins*numSlots entries, skin-major. For skin s, slot k: the sidecar
		// override if it lists slot k, else the model's own material[k] (default inheritance).
		skinBase = pMaterialManager->allocateSkinBlock(numSkins * numSlots);
		for (uint32_t s = 0; s < numSkins; ++s) {
			const std::map<uint32_t, MaterialSource>* row =
				(s < sidecar.skins.size()) ? &sidecar.skins[s] : nullptr;
			for (uint32_t k = 0; k < numSlots; ++k) {
				uint32_t a = 0, n = 0, mr = 0, e = 0;
				bool overridden = false;
				if (row) {
					const auto f = row->find(k);
					if (f != row->end()) {
						const Material m = pMaterialManager->loadMaterial(f->second);
						a = m.albedoMap; n = m.normalMap; mr = m.metalRoughMap; e = m.emissionMap;
						overridden = true;
					}
				}
				if (!overridden && k < materials.size()) {
					const BGLModel::Material& d = materials[k];
					a = d.albedoMap; n = d.normalMap; mr = d.metalRoughMap; e = d.emissionMap;
				}
				pMaterialManager->writeSkinEntry(skinBase + s * numSlots + k, a, n, mr, e);
			}
		}
	}

	xatlas::Atlas* ModelLoaderBase::createLightMapAtlas()
	{
		xatlas::SetPrint(xatlasPrint, false);
		return xatlas::Create();
	}

	// Base no-op so the vtable resolves (declared virtual in the header but previously
	// undefined -> link error). Derived loaders override to actually pack the atlas.
	void ModelLoaderBase::generateLightMapAtlas()
	{
	}

	void ModelLoaderBase::calculateTangent()
	{
		// Temp bitangent accumulator — not stored in the vertex buffer, only used to
		// compute the handedness sign packed into tangent.w.
		std::vector<glm::vec3> tempB(vertices.size(), glm::vec3(0.0f));

		auto accumulateTri = [&](uint32_t i0, uint32_t i1, uint32_t i2)
		{
			glm::vec3 edge1 = vertices[i1].position - vertices[i0].position;
			glm::vec3 edge2 = vertices[i2].position - vertices[i0].position;
			glm::vec2 dUV1 = vertices[i1].uv - vertices[i0].uv;
			glm::vec2 dUV2 = vertices[i2].uv - vertices[i0].uv;
			float det = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
			if (glm::abs(det) < 1e-8f)
				return;
			float f = 1.0f / det;
			glm::vec3 T = f * (dUV2.y * edge1 - dUV1.y * edge2);
			glm::vec3 B = f * (-dUV2.x * edge1 + dUV1.x * edge2);
			for (uint32_t vi : {i0, i1, i2})
			{
				vertices[vi].tangent += glm::vec4(T, 0.0f);
				tempB[vi] += B;
			}
		};

		if (indices.empty())
		{
			for (int i = 0; i + 2 < (int)vertices.size(); i += 3)
				accumulateTri(i, i + 1, i + 2);
		}
		else
		{
			for (int i = 0; i + 2 < (int)indices.size(); i += 3)
				accumulateTri(indices[i], indices[i + 1], indices[i + 2]);
		}

		for (int i = 0; i < (int)vertices.size(); i++)
		{
			glm::vec3 T = glm::vec3(vertices[i].tangent);
			if (glm::length(T) < 1e-8f)
			{
				vertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
				continue;
			}
			T = glm::normalize(T);
			// Handedness: +1 if cross(N,T) agrees with the accumulated bitangent, -1 if mirrored
			float sign = (glm::dot(glm::cross(vertices[i].normal, T), tempB[i]) >= 0.0f) ? 1.0f : -1.0f;
			vertices[i].tangent = glm::vec4(T, sign);
		}
	}
}