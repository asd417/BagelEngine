#include "model/model_loaders/bagel_model_loader.hpp"
#include "bagel_material.hpp"

// GLM functions will expect radian angles for all its functions
#define GLM_FORCE_RADIANS
// Expect depths buffer to range from 0 to 1. (opengl depth buffer ranges from -1 to 1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdio>
#include <cstdarg>


namespace bagel
{
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
		// Keep the authored IK chains + attachments (bone names); resolved to joint indices later
		// (resolveIkSetups / resolveAttachments) once the skeleton is parsed. Stored before the
		// early-out so they survive even without a material manager.
		ikChains       = sidecar.ikChains;
		attachmentDefs = sidecar.attachments;

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
						const BGLModel::Material m = pMaterialManager->loadMaterial(f->second);
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

	std::vector<IKSetup> ModelLoaderBase::resolveIkSetups() const
	{
		std::vector<IKSetup> out;
		if (ikChains.empty() || skeleton.names.empty()) return out;

		// Bone name -> joint index, using the skeleton's per-joint node names.
		auto jointOf = [&](const std::string& name) -> int {
			for (size_t j = 0; j < skeleton.names.size(); ++j)
				if (skeleton.names[j] == name) return static_cast<int>(j);
			return -1;
		};

		for (const ModelSidecar::IkChain& c : ikChains) {
			IKSetup s;
			s.thigh     = jointOf(c.thigh);
			s.shin      = jointOf(c.shin);
			s.foot      = jointOf(c.foot);
			s.goalJoint = jointOf(c.goal);
			s.poleJoint = jointOf(c.pole);
			s.weight    = c.weight;
			s.enabled   = c.enabled;
			// Drop the whole chain if any bone name failed to resolve (a partial IK would misbehave).
			if (s.thigh < 0 || s.shin < 0 || s.foot < 0 || s.goalJoint < 0 || s.poleJoint < 0) {
				auto nm = [&](const std::string& n, int idx) { return idx < 0 ? (" '" + n + "'?") : std::string(); };
				printf("[IK] unresolved bone(s) in chain — skipping:%s%s%s%s%s\n",
					nm(c.thigh, s.thigh).c_str(), nm(c.shin, s.shin).c_str(), nm(c.foot, s.foot).c_str(),
					nm(c.goal, s.goalJoint).c_str(), nm(c.pole, s.poleJoint).c_str());
				continue;
			}
			out.push_back(s);
		}
		return out;
	}

	std::vector<AttachmentComponent::Point> ModelLoaderBase::resolveAttachments() const
	{
		std::vector<AttachmentComponent::Point> out;
		if (attachmentDefs.empty() || skeleton.names.empty()) return out;

		auto jointOf = [&](const std::string& name) -> int {
			for (size_t j = 0; j < skeleton.names.size(); ++j)
				if (skeleton.names[j] == name) return static_cast<int>(j);
			return -1;
		};
		// Build the bone-local offset matrix from translation + Euler rotation, using the SAME
		// X1Y2Z3 column layout as TransformComponent::mat4() so attachment orientation is consistent.
		auto eulerToMat3 = [](const glm::vec3& r) -> glm::mat3 {
			const float c3 = cosf(r.z), s3 = sinf(r.z);
			const float c2 = cosf(r.y), s2 = sinf(r.y);
			const float c1 = cosf(r.x), s1 = sinf(r.x);
			return glm::mat3{
				{ c2 * c3,            c1 * s3 + c3 * s1 * s2,  s1 * s3 - c1 * c3 * s2 },
				{-c2 * s3,            c1 * c3 - s1 * s2 * s3,  c3 * s1 + c1 * s2 * s3 },
				{ s2,                -c2 * s1,                 c1 * c2                }
			};
		};

		for (const ModelSidecar::Attachment& d : attachmentDefs) {
			const int joint = jointOf(d.bone);
			if (joint < 0) {
				printf("[Attachment] '%s' references unknown bone '%s' — skipped\n",
					d.name.c_str(), d.bone.c_str());
				continue;
			}
			AttachmentComponent::Point p;
			p.name  = d.name;
			p.joint = joint;
			p.localOffset = glm::mat4(eulerToMat3(d.rotate));
			p.localOffset[3] = glm::vec4(d.offset, 1.0f);
			out.push_back(std::move(p));
		}
		return out;
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