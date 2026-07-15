#include "bagel_model.hpp"
#include "bagel_util.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>
#include <cstring>

namespace bagel
{

	// Parse a YAML [x, y, z] sequence into a vec3; missing/short nodes keep the fallback.
	static glm::vec3 parseVec3(const YAML::Node &n, const glm::vec3 &fallback = glm::vec3(0.0f))
	{
		if (!n || !n.IsSequence() || n.size() < 3)
			return fallback;
		return {n[0].as<float>(), n[1].as<float>(), n[2].as<float>()};
	}

	static MaterialSource parseMaterial(const YAML::Node &n)
	{
		MaterialSource s;
		if (n["albedo"])
			s.albedo = n["albedo"].as<std::string>();
		if (n["normal"])
			s.normal = n["normal"].as<std::string>();
		if (n["metalRough"])
			s.metalRough = n["metalRough"].as<std::string>();
		if (n["emission"])
			s.emission = n["emission"].as<std::string>();
		return s;
	}

	ModelSidecar ModelSidecar::loadForModel(const std::string &modelSourcePath)
	{
		ModelSidecar out;

		// "/models/x.gltf" -> "/models/x.yaml"
		std::string rel = modelSourcePath;
		const auto dot = rel.find_last_of('.');
		if (dot != std::string::npos)
			rel.resize(dot);
		rel += ".yaml";

		const std::string abs = util::enginePath(rel.c_str());
		std::error_code ec;
		if (!std::filesystem::exists(abs, ec))
			return out;

		try
		{
			YAML::Node root = YAML::LoadFile(abs);

			// Named MaterialSource definitions.
			std::map<std::string, MaterialSource> named;
			if (root["materials"] && root["materials"].IsMap())
			{
				for (const auto &it : root["materials"])
					named[it.first.as<std::string>()] = parseMaterial(it.second);
			}

			// Skin rows: slot index -> material name (resolved against `named`).
			if (root["skins"] && root["skins"].IsSequence())
			{
				for (const auto &skinNode : root["skins"])
				{
					std::map<uint32_t, MaterialSource> row;
					if (skinNode.IsMap())
					{
						for (const auto &cell : skinNode)
						{
							const uint32_t slot = cell.first.as<uint32_t>();
							const std::string matName = cell.second.as<std::string>();
							const auto f = named.find(matName);
							if (f != named.end())
								row[slot] = f->second;
							else
								std::cerr << "[ModelSidecar] " << abs << ": skin references unknown material '" << matName << "'\n";
						}
					}
					out.skins.push_back(std::move(row));
				}
			}
			// IK chains: each entry references skeleton bones by name (resolved to joint indices
			// later, once the skeleton is parsed). thigh/shin/foot/goal/pole required; weight/enabled
			// optional. An entry missing a required bone name is dropped with a warning.
			if (root["ik"] && root["ik"].IsSequence())
			{

				for (const auto &n : root["ik"])
				{
					if (!n.IsMap())
						continue;
					// Copy a bone NAME from the map into a fixed char[BONE_NAME_LENGTH] buffer:
					// NUL-terminated, truncated if longer, left empty ("") if absent/non-scalar.
					auto copyBone = [&](const char *key, char (&dst)[BONE_NAME_LENGTH])
					{
						dst[0] = '\0';
						const YAML::Node v = n[key];
						if (!v || !v.IsScalar())
							return;
						const std::string &s = v.Scalar();
						const size_t len = s.size() < BONE_NAME_LENGTH ? s.size() : BONE_NAME_LENGTH - 1;
						std::memcpy(dst, s.data(), len);
						dst[len] = '\0';
					};
					ModelSidecar::IkChain c;

					copyBone("thigh", c.thigh);
					copyBone("shin", c.shin);
					copyBone("foot", c.foot);
					copyBone("goal", c.goal);
					copyBone("pole", c.pole);
					if (n["weight"])
						c.weight = n["weight"].as<float>();
					if (n["enabled"])
						c.enabled = n["enabled"].as<bool>();

					if (c.thigh[0] == '\0' || c.shin[0] == '\0' || c.foot[0] == '\0' ||
						c.goal[0] == '\0' || c.pole[0] == '\0')
					{
						std::cerr << "[ModelSidecar] " << abs << ": ik entry missing a required bone "
																 "(thigh/shin/foot/goal/pole) — skipped\n";
						continue;
					}
					out.ikChains.push_back(std::move(c));
				}
			}

			// Attach points: name + bone (resolved to a joint later) + local offset/rotation.
			// rotate is authored in DEGREES (like Source's $attachment); stored as radians.
			if (root["attachments"] && root["attachments"].IsSequence())
			{
				for (const auto &n : root["attachments"])
				{
					if (!n.IsMap())
						continue;
					ModelSidecar::Attachment a;
					if (n["name"])
						a.name = n["name"].as<std::string>();
					if (n["bone"])
						a.bone = n["bone"].as<std::string>();
					a.offset = parseVec3(n["offset"]);
					a.rotate = glm::radians(parseVec3(n["rotate"]));
					if (a.name.empty() || a.bone.empty())
					{
						std::cerr << "[ModelSidecar] " << abs << ": attachment missing name or bone — skipped\n";
						continue;
					}
					out.attachments.push_back(std::move(a));
				}
			}
		}
		catch (const std::exception &e)
		{
			std::cerr << "[ModelSidecar] failed to parse " << abs << ": " << e.what() << "\n";
			out.skins.clear();
			out.ikChains.clear();
			out.attachments.clear();
		}
		return out;
	}

} // namespace bagel
