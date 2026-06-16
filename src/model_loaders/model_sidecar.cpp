#include "model_sidecar.hpp"

#include "bagel_util.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>

namespace bagel {

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
		}
		catch (const std::exception& e) {
			std::cerr << "[ModelSidecar] failed to parse " << abs << ": " << e.what() << "\n";
			out.skins.clear();
		}
		return out;
	}

} // namespace bagel
