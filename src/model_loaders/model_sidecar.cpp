#include "model_sidecar.hpp"

#include "bagel_util.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>

namespace bagel {

	// Parse a YAML [x, y, z] sequence into a vec3; missing/short nodes keep the fallback.
	static glm::vec3 parseVec3(const YAML::Node& n, const glm::vec3& fallback = glm::vec3(0.0f))
	{
		if (!n || !n.IsSequence() || n.size() < 3) return fallback;
		return { n[0].as<float>(), n[1].as<float>(), n[2].as<float>() };
	}

	static MaterialSource parseMaterial(const YAML::Node& n)
	{
		MaterialSource s;
		if (n["albedo"])     s.albedo     = n["albedo"].as<std::string>();
		if (n["normal"])     s.normal     = n["normal"].as<std::string>();
		if (n["metalRough"]) s.metalRough = n["metalRough"].as<std::string>();
		if (n["emission"])   s.emission   = n["emission"].as<std::string>();
		return s;
	}

	ModelSidecar ModelSidecar::loadForModel(const std::string& modelSourcePath)
	{
		ModelSidecar out;

		// "/models/x.gltf" -> "/models/x.yaml"
		std::string rel = modelSourcePath;
		const auto dot = rel.find_last_of('.');
		if (dot != std::string::npos) rel.resize(dot);
		rel += ".yaml";

		const std::string abs = util::enginePath(rel.c_str());
		std::error_code ec;
		if (!std::filesystem::exists(abs, ec)) return out;

		try {
			YAML::Node root = YAML::LoadFile(abs);

			// Named MaterialSource definitions.
			std::map<std::string, MaterialSource> named;
			if (root["materials"] && root["materials"].IsMap()) {
				for (const auto& it : root["materials"])
					named[it.first.as<std::string>()] = parseMaterial(it.second);
			}

			// Skin rows: slot index -> material name (resolved against `named`).
			if (root["skins"] && root["skins"].IsSequence()) {
				for (const auto& skinNode : root["skins"]) {
					std::map<uint32_t, MaterialSource> row;
					if (skinNode.IsMap()) {
						for (const auto& cell : skinNode) {
							const uint32_t slot = cell.first.as<uint32_t>();
							const std::string matName = cell.second.as<std::string>();
							const auto f = named.find(matName);
							if (f != named.end()) row[slot] = f->second;
							else std::cerr << "[ModelSidecar] " << abs << ": skin references unknown material '" << matName << "'\n";
						}
					}
					out.skins.push_back(std::move(row));
				}
			}

			// IK chains: each entry references skeleton bones by name (resolved to joint indices
			// later, once the skeleton is parsed). thigh/shin/foot/goal/pole required; weight/enabled
			// optional. An entry missing a required bone name is dropped with a warning.
			if (root["ik"] && root["ik"].IsSequence()) {
				for (const auto& n : root["ik"]) {
					if (!n.IsMap()) continue;
					ModelSidecar::IkChain c;
					if (n["thigh"])   c.thigh   = n["thigh"].as<std::string>();
					if (n["shin"])    c.shin    = n["shin"].as<std::string>();
					if (n["foot"])    c.foot    = n["foot"].as<std::string>();
					if (n["goal"])    c.goal    = n["goal"].as<std::string>();
					if (n["pole"])    c.pole    = n["pole"].as<std::string>();
					if (n["weight"])  c.weight  = n["weight"].as<float>();
					if (n["enabled"]) c.enabled = n["enabled"].as<bool>();
					if (c.thigh.empty() || c.shin.empty() || c.foot.empty() ||
					    c.goal.empty()  || c.pole.empty()) {
						std::cerr << "[ModelSidecar] " << abs << ": ik entry missing a required bone "
						             "(thigh/shin/foot/goal/pole) — skipped\n";
						continue;
					}
					out.ikChains.push_back(std::move(c));
				}
			}

			// Attach points: name + bone (resolved to a joint later) + local offset/rotation.
			// rotate is authored in DEGREES (like Source's $attachment); stored as radians.
			if (root["attachments"] && root["attachments"].IsSequence()) {
				for (const auto& n : root["attachments"]) {
					if (!n.IsMap()) continue;
					ModelSidecar::Attachment a;
					if (n["name"]) a.name = n["name"].as<std::string>();
					if (n["bone"]) a.bone = n["bone"].as<std::string>();
					a.offset = parseVec3(n["offset"]);
					a.rotate = glm::radians(parseVec3(n["rotate"]));
					if (a.name.empty() || a.bone.empty()) {
						std::cerr << "[ModelSidecar] " << abs << ": attachment missing name or bone — skipped\n";
						continue;
					}
					out.attachments.push_back(std::move(a));
				}
			}
		}
		catch (const std::exception& e) {
			std::cerr << "[ModelSidecar] failed to parse " << abs << ": " << e.what() << "\n";
			out.skins.clear();
			out.ikChains.clear();
			out.attachments.clear();
		}
		return out;
	}

} // namespace bagel
