#include "animation/bagel_animatable.hpp"

#include "ecs/components/light.hpp"
#include "ecs/components/transform.hpp"

#include <algorithm>
#include <cctype>

namespace bagel {

	namespace {
		// Function-local static, not a namespace-scope global: the BGL_ANIMATABLE macros run
		// during static initialization, and this guarantees the table exists before the first
		// registration regardless of translation-unit init order.
		std::vector<AnimatableProperty> &table() {
			static std::vector<AnimatableProperty> t;
			return t;
		}

		std::string lower(std::string s) {
			std::transform(s.begin(), s.end(), s.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
	}

	bool registerAnimatable(const AnimatableProperty &p) {
		table().push_back(p);
		return true;
	}

	const AnimatableProperty *findAnimatable(const std::string &key) {
		const std::string want = lower(key);
		for (const AnimatableProperty &p : table())
			if (lower(p.key()) == want)
				return &p;
		return nullptr;
	}

	const std::vector<AnimatableProperty> &animatableProperties() { return table(); }

	// =========================================================================
	// The animatable set. Add a line here to expose a new field to /anims/*.yaml.
	// The key an animation file uses is exactly "Component.property".
	// =========================================================================

	// TransformComponent's data is private, so these go through its accessors. `rotation` is
	// RADIANS here (setRotation), matching the component; text maps convert degrees on load but
	// an animation drives the stored value directly.
	BGL_ANIMATABLE(TransformComponent, position, glm::vec3, getTranslation, setTranslation);
	BGL_ANIMATABLE(TransformComponent, rotation, glm::vec3, getRotation, setRotation);
	BGL_ANIMATABLE(TransformComponent, scale, glm::vec3, getScale, setScale);
	BGL_ANIMATABLE(TransformComponent, localposition, glm::vec3, getLocalTranslation, setLocalTranslation);
	BGL_ANIMATABLE(TransformComponent, localrotation, glm::vec3, getLocalRotation, setLocalRotation);
	BGL_ANIMATABLE(TransformComponent, localscale, glm::vec3, getLocalScale, setLocalScale);

	// Lights: plain public members.
	BGL_ANIMATABLE_FIELD(PointLightComponent, color);
	BGL_ANIMATABLE_FIELD(PointLightComponent, lux);
	BGL_ANIMATABLE_FIELD(DirectionalLightComponent, color);
	BGL_ANIMATABLE_FIELD(DirectionalLightComponent, rotation); // degrees, per the component
	BGL_ANIMATABLE_FIELD(DirectionalLightComponent, lux);

} // namespace bagel
