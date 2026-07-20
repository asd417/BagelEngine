#include "map/bagel_text_map.hpp"

#include "animation/bagel_skin_manager.hpp"
#include "bagel_material.hpp"
#include "ecs/bagel_ecs_components.hpp" // TransformComponent, PointLight/DirectionalLightComponent
#include "model/model_component_builder.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace bagel {

namespace {

	// ---- scalar/sequence helpers (mirrors the model-sidecar parsing in bagel_model.cpp) ----

	glm::vec3 parseVec3(const YAML::Node &n, const glm::vec3 &fallback = glm::vec3(0.0f)) {
		if (!n || !n.IsSequence() || n.size() < 3)
			return fallback;
		return {n[0].as<float>(), n[1].as<float>(), n[2].as<float>()};
	}

	// rgb or rgba sequence -> vec4 (alpha defaults to 1). Light components store color as vec4.
	glm::vec4 parseColor(const YAML::Node &n, const glm::vec4 &fallback) {
		if (!n || !n.IsSequence() || n.size() < 3)
			return fallback;
		const float a = n.size() >= 4 ? n[3].as<float>() : 1.0f;
		return {n[0].as<float>(), n[1].as<float>(), n[2].as<float>(), a};
	}

	float parseFloat(const YAML::Node &n, float fallback) {
		return (n && n.IsScalar()) ? n.as<float>(fallback) : fallback;
	}

	bool parseBool(const YAML::Node &n, bool fallback) {
		return (n && n.IsScalar()) ? n.as<bool>(fallback) : fallback;
	}

	std::string parseString(const YAML::Node &n) {
		return (n && n.IsScalar()) ? n.Scalar() : std::string{};
	}

	// The hybrid resolve step: a component-template value that is a "$var" scalar is looked up
	// in this instance's key/values; any other value (a literal in the prefab) is used as-is.
	// A missing var resolves to an undefined node, so the parse* fallbacks kick in.
	YAML::Node resolveField(const YAML::Node &templateVal, const YAML::Node &entity) {
		if (templateVal && templateVal.IsScalar()) {
			const std::string &s = templateVal.Scalar();
			if (!s.empty() && s[0] == '$')
				return entity[s.substr(1)];
		}
		return templateVal;
	}

	// Stamp one resolved component onto `e` (creating it lazily on the first real component so an
	// info_player_start, which stamps nothing, leaves no orphan entity). ModelComponents are cooked
	// through the builder; everything else is a plain emplace. Returns false + logs on bad input.
	void applyComponent(entt::registry &registry, entt::entity &e, const std::string &type,
	                    const YAML::Node &compTemplate, const YAML::Node &entity,
	                    ModelComponentBuilder &builder, TextMap::SpawnPoint &outSpawn) {
		auto field = [&](const char *key) { return resolveField(compTemplate[key], entity); };

		// info_player_start's PlayerStart pseudo-component: reported back, not emplaced.
		if (type == "PlayerStart") {
			outSpawn.set = true;
			outSpawn.position = parseVec3(field("position"), outSpawn.position);
			outSpawn.rotation = parseVec3(field("rotation"), outSpawn.rotation);
			return;
		}

		if (e == entt::null)
			e = registry.create();

		if (type == "TransformComponent") {
			auto &tc = registry.emplace_or_replace<TransformComponent>(e);
			tc.setTranslation(parseVec3(field("position")));
			tc.setRotationDegrees(parseVec3(field("rotation")));       // text maps author in degrees
			tc.setScale(parseVec3(field("scale"), glm::vec3(1.0f)));
		}
		else if (type == "ModelComponent") {
			const std::string src = parseString(field("source"));
			if (src.empty()) {
				std::cerr << "[textmap] ModelComponent with no source; skipped\n";
				return;
			}
			ModelLoadSettings settings{};
			settings.source = src;
			settings.mergeSolidSubmeshes = parseBool(field("mergesolidsubmeshes"), true);
			if (parseString(field("buildmode")) == "lines")
				settings.buildMode = ComponentBuildMode::LINES;
			builder.buildComponent(e, src.c_str(), settings); // emplaces ModelComponent + GPU buffers
		}
		else if (type == "PointLightComponent") {
			auto &pl = registry.emplace_or_replace<PointLightComponent>(e);
			pl.color = parseColor(field("color"), pl.color);
			pl.lux   = parseFloat(field("lux"), pl.lux);
		}
		else if (type == "DirectionalLightComponent") {
			auto &dl = registry.emplace_or_replace<DirectionalLightComponent>(e);
			dl.color    = parseColor(field("color"), dl.color);
			dl.rotation = parseVec3(field("rotation"), dl.rotation); // already degrees in the component
			dl.lux      = parseFloat(field("lux"), dl.lux);
		}
		else if (type == "DefaultAnimation") {
			// Configure the animation state the ModelComponent builder attached for a skinned model.
			// Must be listed AFTER ModelComponent in the prefab (buildComponent creates these). No-op
			// with a warning if the asset wasn't skinned (no playback component to configure).
			auto *play = registry.try_get<AnimationPlaybackComponent>(e);
			auto *anim = registry.try_get<AnimationComponent>(e);
			if (!play || !anim) {
				std::cerr << "[textmap] DefaultAnimation on a non-skinned prop; ignored\n";
				return;
			}
			// clip: a numeric index, or a clip name matched against the model's baked clip list.
			const YAML::Node clipN = field("clip");
			if (clipN && clipN.IsScalar()) {
				uint32_t idx = clipN.as<uint32_t>(UINT32_MAX); // sentinel: not a number -> treat as a name
				if (idx == UINT32_MAX) {
					idx = 0;
					const std::string &want = clipN.Scalar();
					for (uint32_t i = 0; i < anim->clipNames.size(); ++i)
						if (anim->clipNames[i] == want) { idx = i; break; }
				}
				selectClip(*play, *anim, idx); // out-of-range is ignored by selectClip
			}
			// pose: "manual" routes draws to the hand/IK-posed dynamic palette (poseable rig);
			// "auto" (default) plays the selected clip. poseDirty forces the first palette upload.
			const std::string pose = parseString(field("pose"));
			if (pose == "manual")     { play->manualPose = true;  play->poseDirty = true; }
			else if (pose == "auto")  { play->manualPose = false; }
		}
		else {
			std::cerr << "[textmap] unknown component type '" << type << "'; skipped\n";
		}
	}

} // namespace

bool TextMap::exists(const std::string &path) {
	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

bool TextMap::load(entt::registry &registry, const std::string &path,
                   ModelComponentBuilder &builder,
                   BGLMaterialManager &materialManager, BGLSkinManager &skinManager,
                   SpawnPoint &outSpawn) {
	// Configure the builder for cooking model assets, same as Map::rehydrate does.
	builder.setTextureLoader(&materialManager.getTextureLoader());
	builder.setMaterialManager(&materialManager);
	builder.setSkinManager(&skinManager);

	YAML::Node root;
	try {
		root = YAML::LoadFile(path);
	} catch (const std::exception &e) {
		std::cerr << "[textmap] failed to parse '" << path << "': " << e.what() << '\n';
		return false;
	}

	const YAML::Node prefabs  = root["prefabs"];
	const YAML::Node entities = root["entities"];
	if (!prefabs || !prefabs.IsMap()) {
		std::cerr << "[textmap] '" << path << "' has no 'prefabs' map\n";
		return false;
	}
	if (!entities || !entities.IsSequence()) {
		std::cerr << "[textmap] '" << path << "' has no 'entities' list\n";
		return false;
	}

	for (const YAML::Node &instance : entities) {
		const std::string cls = parseString(instance["class"]);
		if (cls.empty()) {
			std::cerr << "[textmap] entity with no 'class'; skipped\n";
			continue;
		}
		const YAML::Node prefab = prefabs[cls];
		if (!prefab || !prefab.IsSequence()) {
			std::cerr << "[textmap] unknown class '" << cls << "'; skipped\n";
			continue;
		}

		// Guard per instance: a malformed value (e.g. a non-numeric position) makes yaml-cpp's
		// as<T>() throw. Skip that one entity and keep going rather than aborting the whole map.
		try {
			entt::entity e = entt::null; // created lazily by applyComponent
			for (const YAML::Node &compTemplate : prefab) {
				const std::string type = parseString(compTemplate["type"]);
				applyComponent(registry, e, type, compTemplate, instance, builder, outSpawn);
			}
		} catch (const std::exception &ex) {
			std::cerr << "[textmap] bad '" << cls << "' entity skipped: " << ex.what() << '\n';
		}
	}
	return true;
}

} // namespace bagel
