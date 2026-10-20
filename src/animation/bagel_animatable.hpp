#pragma once
// -----------------------------------------------------------------------------
// Animatable property registry.
//
// A component field is made drivable from data by declaring it ANIMATABLE once, at namespace
// scope in a .cpp (see bagel_animatable.cpp):
//
//     BGL_ANIMATABLE_FIELD(PointLightComponent, lux)                                   // public field
//     BGL_ANIMATABLE(TransformComponent, position, glm::vec3, getTranslation, setTranslation)
//
// Each declaration publishes the key "Component.property" (e.g. "TransformComponent.position"),
// which is what a scripted sequence file names in its `property:` field. Nothing else has to
// know the field exists: the sequence player looks the key up and calls the registered thunks.
//
// Values are carried as a glm::vec4 regardless of the field's real type — float uses .x, vec3
// uses .xyz. One uniform value type keeps the type erasure to two function pointers instead of
// a variant, and every animatable field in this engine is a float or a small float vector.
// Adding a new value type means extending animPack/animUnpack/animTypeOf here.
// -----------------------------------------------------------------------------

#include "entt.hpp"

#include <glm/glm.hpp>

#include <string>
#include <type_traits>
#include <vector>

namespace bagel {

	enum class AnimValueType { Float, Vec3, Vec4 };

	struct AnimatableProperty {
		const char *component = ""; // "TransformComponent" (string literal; not owned)
		const char *property = "";  // "position"
		AnimValueType type = AnimValueType::Float;
		// Read the current value off `e`. False when the entity has no such component.
		bool (*read)(entt::registry &, entt::entity, glm::vec4 &) = nullptr;
		// Write an interpolated value back. Silently no-ops when the component is missing.
		void (*write)(entt::registry &, entt::entity, const glm::vec4 &) = nullptr;

		std::string key() const { return std::string(component) + '.' + property; }
		// Number of meaningful components in the packed vec4 (1, 3 or 4) — used when parsing
		// literals out of YAML so `from: 0` is valid for a float but not for a vec3.
		int arity() const {
			return type == AnimValueType::Float ? 1 : (type == AnimValueType::Vec3 ? 3 : 4);
		}
	};

	// Called by the macros during static init; returns true so it can initialize a bool.
	bool registerAnimatable(const AnimatableProperty &p);

	// Exact, case-insensitive lookup of "Component.property". nullptr if unknown.
	const AnimatableProperty *findAnimatable(const std::string &key);

	// Everything registered — for the `anim props` console listing.
	const std::vector<AnimatableProperty> &animatableProperties();

	// ---- value packing ------------------------------------------------------
	inline glm::vec4 animPack(float v) { return {v, 0.0f, 0.0f, 0.0f}; }
	inline glm::vec4 animPack(const glm::vec3 &v) { return {v.x, v.y, v.z, 0.0f}; }
	inline glm::vec4 animPack(const glm::vec4 &v) { return v; }

	template <class T> T animUnpack(const glm::vec4 &v);
	template <> inline float animUnpack<float>(const glm::vec4 &v) { return v.x; }
	template <> inline glm::vec3 animUnpack<glm::vec3>(const glm::vec4 &v) { return glm::vec3(v); }
	template <> inline glm::vec4 animUnpack<glm::vec4>(const glm::vec4 &v) { return v; }

	template <class T> constexpr AnimValueType animTypeOf() {
		if constexpr (std::is_same_v<T, float>) return AnimValueType::Float;
		else if constexpr (std::is_same_v<T, glm::vec3>) return AnimValueType::Vec3;
		else {
			static_assert(std::is_same_v<T, glm::vec4>,
			              "animatable fields must be float, glm::vec3 or glm::vec4");
			return AnimValueType::Vec4;
		}
	}

	// ---- declaration macros -------------------------------------------------
	// Both expand to a namespace-scope bool initialized by a call at static-init time. The
	// thunks are CAPTURELESS lambdas, which convert implicitly to the plain function pointers
	// in AnimatableProperty — no std::function, no allocation.

	// Getter/setter pair, for components whose data is private (e.g. TransformComponent).
	// [[maybe_unused]]: the bool exists only to give the registration call somewhere to run.
	#define BGL_ANIMATABLE(Comp, Prop, Type, Getter, Setter)                                  \
		[[maybe_unused]] static const bool bgl_animatable_##Comp##_##Prop =                    \
		    ::bagel::registerAnimatable({                                                      \
			#Comp, #Prop, ::bagel::animTypeOf<Type>(),                                         \
			[](entt::registry &r, entt::entity e, glm::vec4 &out) -> bool {                    \
				auto *c = r.try_get<Comp>(e);                                                  \
				if (!c) return false;                                                          \
				out = ::bagel::animPack(c->Getter());                                          \
				return true;                                                                   \
			},                                                                                 \
			[](entt::registry &r, entt::entity e, const glm::vec4 &v) {                        \
				auto *c = r.try_get<Comp>(e);                                                  \
				if (!c) return;                                                                \
				c->Setter(::bagel::animUnpack<Type>(v));                                       \
			}})

	// Public data member.
	#define BGL_ANIMATABLE_FIELD(Comp, Prop)                                                  \
		[[maybe_unused]] static const bool bgl_animatable_##Comp##_##Prop =                    \
		    ::bagel::registerAnimatable({                                                      \
			#Comp, #Prop, ::bagel::animTypeOf<decltype(Comp::Prop)>(),                         \
			[](entt::registry &r, entt::entity e, glm::vec4 &out) -> bool {                    \
				auto *c = r.try_get<Comp>(e);                                                  \
				if (!c) return false;                                                          \
				out = ::bagel::animPack(c->Prop);                                              \
				return true;                                                                   \
			},                                                                                 \
			[](entt::registry &r, entt::entity e, const glm::vec4 &v) {                        \
				auto *c = r.try_get<Comp>(e);                                                  \
				if (!c) return;                                                                \
				c->Prop = ::bagel::animUnpack<decltype(Comp::Prop)>(v);                        \
			}})

} // namespace bagel
