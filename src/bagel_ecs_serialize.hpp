#pragma once

// ECS component serialization.
//
// Design (see discussion): components stay plain structs. Serialization is a set
// of free-function `serialize` overloads — one per component — selected by the
// component argument type via overload resolution + ADL (all live in namespace
// bagel alongside the components). A single `serialize` covers both save and load
// because it is templated on the Archive; the archive decides whether `ar(field)`
// writes or reads.
//
// Persistent vs transient: each overload lists ONLY the authored/source-of-truth
// fields. Runtime handles (VkBuffer, bindless handles, JPH::BodyID, mapped GPU
// buffers) are never serialized — they are rebuilt afterwards in the rehydrate
// pass from the persisted "recipe" (e.g. ModelComponent::loadSettings).

#include "bagel_ecs_components.hpp"
#include "components/planet.hpp" // PlanetComponent (paint cube-map recipe)

#include "entt.hpp"
#include <Jolt/Core/StreamWrapper.h>

#include <cstdint>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace bagel {

	// ---- archive <-> component dispatch -------------------------------------
	// Detects whether a serialize(Archive&, T&) overload exists for T. Used by the
	// archives to decide: recurse into the component's serialize, or treat the
	// value as a leaf (raw bytes / string).
	template<class Archive, class T, class = void>
	struct has_serialize : std::false_type {};
	template<class Archive, class T>
	struct has_serialize<Archive, T,
		std::void_t<decltype(serialize(std::declval<Archive&>(), std::declval<T&>()))>>
		: std::true_type {};

	// Detects std::vector<T> so the archives can length-prefix it and recurse per element.
	template<class T> struct is_std_vector : std::false_type {};
	template<class T, class A> struct is_std_vector<std::vector<T, A>> : std::true_type {};

	// ---- archives -----------------------------------------------------------
	// Minimal little-endian binary archive. Satisfies the entt snapshot archive
	// contract (callable with counts, entt::entity values and component refs) and
	// the serialize-function contract (ar(field, field, ...)).
	//
	// Leaf handling:
	//   - std::string            -> uint32 length + raw chars
	//   - has a serialize<>       -> recurse into it (components)
	//   - else (must be trivially copyable: arithmetic, enums, glm vec/mat,
	//     entt::entity, JPH::BodyID) -> raw bytes
	class OutputArchive {
	public:
		explicit OutputArchive(std::ostream& os) : os(os) {}

		template<class... Ts>
		void operator()(Ts&&... vs) { (process(vs), ...); }

	private:
		template<class T>
		void process(const T& v) {
			using U = std::remove_cv_t<std::remove_reference_t<T>>;
			if constexpr (std::is_same_v<U, std::string>) {
				const std::uint32_t n = static_cast<std::uint32_t>(v.size());
				raw(n);
				os.write(v.data(), n);
			} else if constexpr (is_std_vector<U>::value) {
				const std::uint32_t n = static_cast<std::uint32_t>(v.size());
				raw(n);
				for (const auto& e : v) process(e);
			} else if constexpr (has_serialize<OutputArchive, U>::value) {
				// save reads only; cast away const to share the load signature
				serialize(*this, const_cast<U&>(v));
			} else {
				static_assert(std::is_trivially_copyable_v<U>,
					"field needs a serialize overload or must be trivially copyable");
				raw(v);
			}
		}
		template<class T>
		void raw(const T& v) { os.write(reinterpret_cast<const char*>(&v), sizeof v); }

		std::ostream& os;
	};

	class InputArchive {
	public:
		explicit InputArchive(std::istream& is) : is(is) {}

		template<class... Ts>
		void operator()(Ts&... vs) { (process(vs), ...); }

	private:
		template<class T>
		void process(T& v) {
			if constexpr (std::is_same_v<T, std::string>) {
				std::uint32_t n = 0;
				raw(n);
				v.resize(n);
				if (n) is.read(&v[0], n);
			} else if constexpr (is_std_vector<T>::value) {
				std::uint32_t n = 0;
				raw(n);
				v.resize(n);
				for (auto& e : v) process(e);
			} else if constexpr (has_serialize<InputArchive, T>::value) {
				serialize(*this, v);
			} else {
				static_assert(std::is_trivially_copyable_v<T>,
					"field needs a serialize overload or must be trivially copyable");
				raw(v);
			}
		}
		template<class T>
		void raw(T& v) { is.read(reinterpret_cast<char*>(&v), sizeof v); }

		std::istream& is;
	};

	// ---- per-component serialize overloads ----------------------------------
	// Each lists only persistent fields. Transient fields are noted in comments
	// and rebuilt in rehydrate().

	// DataBufferComponent: fully transient (mapped GPU buffer + bindless handle).
	// Not serialized at all and not listed in Save/LoadRegistry — recreated wholesale.

	template<class Archive>
	void serialize(Archive& ar, TransformComponent& c) {
		// All authored. (private members — this function is a friend.)
		ar(c.translation, c.scale, c.rotation,
		   c.localTranslation, c.localScale, c.localRotation);
	}

	template<class Archive>
	void serialize(Archive& ar, TransformArrayComponent& c) {
		// Persist only the used range [0, maxIndex); the arrays are fixed-capacity.
		// Transient: usingBuffer, bufferHandle — rebuilt via ToBufferComponent().
		ar(c.maxIndex);
		for (std::uint32_t i = 0; i < c.maxIndex; ++i) {
			ar(c.translation[i], c.scale[i], c.rotation[i],
			   c.localTranslation[i], c.localScale[i], c.localRotation[i]);
		}
	}

	template<class Archive>
	void serialize(Archive& ar, TransformHierachyComponent& c) {
		// `parent` is an entt::entity. Save/load of the whole registry preserves
		// identifiers (snapshot_loader), so this handle stays valid. For partial /
		// cross-registry loads you'd remap it with entt::continuous_loader instead.
		ar(c.parent, c.hasParent, c.depth,
		   c.localTranslation, c.localRotation, c.localScale, c.attachment);
	}

	template<class Archive>
	void serialize(Archive& ar, PointLightComponent& c) {
		ar(c.color, c.lux); // all authored
	}

	template<class Archive>
	void serialize(Archive& ar, DirectionalLightComponent& c) {
		ar(c.color, c.rotation, c.lux, c.cascadeEnds,
		   c.casterRange, c.shadowBiasMin, c.shadowBiasSlope); // all authored
	}

	template<class Archive>
	void serialize(Archive& ar, ModelLoadSettings& s) {
		// buildMode is an enum -> handled as a trivially-copyable leaf by the archive.
		ar(s.source, s.scale, s.scaleVec, s.buildMode, s.maxPrimitives, s.mergeSolidSubmeshes);
	}

	template<class Archive>
	void serialize(Archive& ar, MaterialSource& s) {
		ar(s.albedo, s.normal, s.metalRough, s.emission);
	}

	// Shared model recipe: loadSettings + frustumCull + the generated-material sources.
	// The loader rebuilds submeshes/GPU buffers from loadSettings during rehydrate. For
	// OBJ/GLTF, materials come back from the asset (materialCount == 0); for GENERATED
	// models the code-assigned material SOURCES are captured here and restored on rehydrate.
	// materialSources[] is a fixed array, so length-prefix it with materialCount (submeshCount
	// isn't serialized, so we can't derive the valid range any other way).
	template<class Archive>
	void serializeModelRecipe(Archive& ar, ModelComponent& c) {
		// skinIndex is the per-entity authored state; the skin block itself (skinBase/numSlots/
		// numSkins) is transient and rebuilt from the .yaml sidecar by the loader on rehydrate.
		ar(c.loadSettings, c.frustumCull, c.skinIndex, c.materialCount);
		for (std::uint32_t i = 0; i < c.materialCount && i < ModelComponent::MAX_MATERIALS; ++i)
			ar(c.materialSources[i]);
	}

	template<class Archive>
	void serialize(Archive& ar, ModelComponent& c) {
		serializeModelRecipe(ar, c);
	}

	template<class Archive>
	void serialize(Archive& ar, WireframeComponent& c) {
		serializeModelRecipe(ar, c);
		ar(c.color); // + wireframe tint
	}

	template<class Archive>
	void serialize(Archive& ar, CollisionModelComponent& c) {
		serializeModelRecipe(ar, c);
		ar(c.collisionScale);
	}

	// AnimationComponent: only the AUTHORED POSE is persisted —
	//   manualPose : whether draws read the hand-posed palette
	//   editPose   : per-joint local TRS the gizmo authors (Pose = vector<JointTransform>)
	// IK setups are NOT serialized: the "<model>.yaml" sidecar is their single source of truth and
	// the model builder re-attaches them on every load/rehydrate (see ModelLoaderBase::resolveIkSetups
	// / bagel_model.hpp). Everything else (skeleton, baked clip tables, paletteBase/dynamicPaletteBase,
	// jointCount, playback time) is TRANSIENT and rebuilt by the builder, which then re-applies these
	// fields onto the freshly built component (see rehydrateModelType). JointTransform is trivially
	// copyable, so editPose archives element-wise.
	template<class Archive>
	void serialize(Archive& ar, AnimationComponent& c) {
		ar(c.manualPose, c.editPose);
	}

	// JPH::BodyCreationSettings is the persisted "recipe" for a physics body (shape,
	// mass, motion type, layer, ...). It is NOT trivially copyable — it owns a
	// ref-counted Shape — so it can't go through the raw archive. Serialize it the Jolt
	// way: SaveWithChildren writes the settings + shape into a byte blob, which we then
	// store as a length-prefixed string through the archive. The BodyID is never
	// serialized; it is transient and reissued by the live engine in RehydratePhysicsBodies.
	template<class Archive>
	void serializeBody(Archive& ar, JPH::BodyCreationSettings& s) {
		if constexpr (std::is_same_v<std::remove_reference_t<Archive>, OutputArchive>) {
			std::ostringstream blob(std::ios::out | std::ios::binary);
			JPH::StreamOutWrapper out(blob);
			JPH::BodyCreationSettings::ShapeToIDMap shapeMap;
			JPH::BodyCreationSettings::MaterialToIDMap materialMap;
			JPH::BodyCreationSettings::GroupFilterToIDMap groupFilterMap;
			s.SaveWithChildren(out, &shapeMap, &materialMap, &groupFilterMap);
			std::string data = blob.str();
			ar(data);
		} else {
			std::string data;
			ar(data);
			std::istringstream blob(data, std::ios::in | std::ios::binary);
			JPH::StreamInWrapper in(blob);
			JPH::BodyCreationSettings::IDToShapeMap shapeMap;
			JPH::BodyCreationSettings::IDToMaterialMap materialMap;
			JPH::BodyCreationSettings::IDToGroupFilterMap groupFilterMap;
			auto result = JPH::BodyCreationSettings::sRestoreWithChildren(in, shapeMap, materialMap, groupFilterMap);
			if (result.IsValid()) s = result.Get();
		}
	}

	template<class Archive>
	void serialize(Archive& ar, JoltPhysicsComponent& c) {
		serializeBody(ar, c.settings); // recipe only; bodyID transient (rebuilt in rehydrate)
	}

	template<class Archive>
	void serialize(Archive& ar, JoltKinematicComponent& c) {
		ar(c.moveMode);                // authored config
		serializeBody(ar, c.settings); // recipe only; bodyID transient (rebuilt in rehydrate)
	}

	template<class Archive>
	void serialize(Archive& ar, InfoComponent& c) {
	}

	// PlanetComponent: persistent recipe only. cfg (planet::TerrainConfig) is trivially
	// copyable so it archives as a raw leaf — this carries the noise settings AND the paint
	// resolution / height scale. paint is the flat R16 cube-map (6*paintRes^2), archived via
	// the vector path. The PlanetTerrain object, ModelComponent GPU buffers and the bindless
	// face handle are TRANSIENT — rebuilt after load (see MyApplication::loadMapFromPath).
	template<class Archive>
	void serialize(Archive& ar, PlanetComponent& c) {
		ar(c.cfg, c.paint);
	}

	// ---- whole-registry save / load -----------------------------------------
	// The component list below IS the manifest of persisted component types. Keep it
	// in sync with the overloads above.
	//
	// Excluding entities: entt always serializes the WHOLE entity pool (you cannot
	// partially snapshot entity ids — see the static_assert on snapshot::get's range
	// overload). So to keep a runtime-only entity (e.g. a Transient-tagged camera)
	// out of the map we serialize its id but attach NO components to it; on load it
	// comes back as a componentless orphan and LoadRegistry's .orphans() deletes it.
	// That's what saveComponent() does: it uses the range overload fed a view that
	// excludes Transient, so transient entities get a null placeholder, not data.

	// Serialize component C for every entity that is NOT tagged Transient.
	template<class C, class Snapshot>
	void saveComponent(const Snapshot& snap, OutputArchive& ar, const entt::registry& registry) {
		auto view = registry.view<const C>(entt::exclude<Transient>);
		snap.template get<C>(ar, view.begin(), view.end());
	}

	inline void SaveRegistry(const entt::registry& registry, std::ostream& os) {
		OutputArchive ar{ os };
		const auto snap = entt::snapshot{ registry };

		// Entity ids first (all of them) so identity — and entity-valued fields like
		// TransformHierachyComponent::parent — survives the round trip.
		snap.get<entt::entity>(ar);

		saveComponent<TransformComponent>(snap, ar, registry);
		saveComponent<TransformArrayComponent>(snap, ar, registry);
		saveComponent<TransformHierachyComponent>(snap, ar, registry);
		saveComponent<PointLightComponent>(snap, ar, registry);
		saveComponent<DirectionalLightComponent>(snap, ar, registry);
		saveComponent<ModelComponent>(snap, ar, registry);
		saveComponent<WireframeComponent>(snap, ar, registry);
		saveComponent<CollisionModelComponent>(snap, ar, registry);
		saveComponent<AnimationComponent>(snap, ar, registry);
		saveComponent<JoltPhysicsComponent>(snap, ar, registry);
		saveComponent<JoltKinematicComponent>(snap, ar, registry);
		saveComponent<InfoComponent>(snap, ar, registry);
		saveComponent<PlanetComponent>(snap, ar, registry);
	}

	inline void LoadRegistry(entt::registry& registry, std::istream& is) {
		InputArchive ar{ is };
		entt::snapshot_loader{ registry }
			.get<entt::entity>(ar)
			.get<TransformComponent>(ar)
			.get<TransformArrayComponent>(ar)
			.get<TransformHierachyComponent>(ar)
			.get<PointLightComponent>(ar)
			.get<DirectionalLightComponent>(ar)
			.get<ModelComponent>(ar)
			.get<WireframeComponent>(ar)
			.get<CollisionModelComponent>(ar)
			.get<AnimationComponent>(ar)
			.get<JoltPhysicsComponent>(ar)
			.get<JoltKinematicComponent>(ar)
			.get<InfoComponent>(ar)
			.get<PlanetComponent>(ar)
			.orphans(); // drop entities that ended up with no components

		// After this, persistent data is restored but transient state is at defaults.
		// Call your rehydrate pass to rebuild it (see below).
	}

	// ---- rehydrate (rebuild transient state) --------------------------------
	// LoadRegistry restores only PERSISTENT data; transient GPU/physics state must be
	// rebuilt afterwards. That rebuild needs engine subsystems (ModelComponentBuilder,
	// BGLMaterialManager, physics) that live above this header, so it is implemented in
	// the application layer — see MyApplication::rehydrateScene() in my_application.cpp.
	// The model rebuild there uses a collect-all / remove-all / rebuild ordering so the
	// builder's source-dedup can't match a half-loaded component that has no VkBuffers.

} // namespace bagel
