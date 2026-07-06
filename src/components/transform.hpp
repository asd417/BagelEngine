#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include "entt.hpp"

#include "components/data_buffer.hpp"
#include "bagel_engine_config.hpp"

namespace bagel {
	// Forward declarations so the structs below can befriend their serialization
	// hooks (needed for the ones with private fields). Definitions live in
	// bagel_ecs_serialize.hpp; components that have only public fields don't need
	// the friend declaration but use the same free-function overload set.
	struct TransformComponent;
	struct TransformArrayComponent;
	template<class Archive> void serialize(Archive&, TransformComponent&);
	template<class Archive> void serialize(Archive&, TransformArrayComponent&);

	struct TransformComponent {
		TransformComponent() = default;
		TransformComponent(float x, float y, float z) { translation = { x,y,z }; }
		TransformComponent(glm::vec4 pos) { translation = glm::vec3(pos); }
		void	cacheMat4();
		//retrieve the cached mat4 calculation result (valid only after cacheMat4() this frame)
		const glm::mat4 &getMat4() const { return cached; }
		// Compute the model matrix on demand WITHOUT touching the cache. For the few update-phase
		// callers (gizmo, hierarchy/attachments, physics queries) that need the current transform
		// before the per-frame cacheTransforms() pass runs. cacheMat4() is this + a store.
		glm::mat4 computeMat4() const;

		glm::mat3	normalMatrix();
		glm::vec3	getTranslation() const { return translation; }
		void		setTranslation(const glm::vec3& _translation) { translation = _translation; }
		glm::vec3	getScale() const { return scale; }
		void		setScale(const glm::vec3& _scale) { scale = _scale; }
		glm::vec3	getRotation() const { return rotation; }
		glm::vec3	getRotationDegrees() const { return {rotation.x * 180 / 3.1415926535f, rotation.y * 180 / 3.1415926535f, rotation.z * 180 / 3.1415926535f}; }
		void		setRotation(const glm::vec3& _rotation) { rotation = _rotation; }
		void 		setRotationDegrees(const glm::vec3 &_rotation) { rotation = {_rotation.x / 180 * 3.1415926535, _rotation.y / 180 * 3.1415926535, _rotation.z / 180 * 3.1415926535}; }
		glm::vec3	getLocalTranslation() const { return localTranslation; }
		void		setLocalTranslation(const glm::vec3& _translation) { localTranslation = _translation; }
		glm::vec3	getLocalScale() const { return localScale; }
		void		setLocalScale(const glm::vec3& _scale){ localScale = _scale; }
		glm::vec3	getLocalRotation() const { return localRotation; }
		glm::vec3	getLocalRotationDegrees() const { return {localRotation.x * 180 / 3.1415926535f, localRotation.y * 180 / 3.1415926535f, localRotation.z * 180 / 3.1415926535f}; }
		void		setLocalRotation(const glm::vec3& _rotation) { localRotation = _rotation; }
		void 		setLocalRotationDegrees(const glm::vec3 &_rotation) { localRotation = {_rotation.x / 180 * 3.1415926535, _rotation.y / 180 * 3.1415926535, _rotation.z / 180 * 3.1415926535}; }
		
		glm::vec3	getWorldTranslation() const { return translation + localTranslation; };
		glm::vec3	getWorldScale() const { return { scale.x * localScale.x, scale.y * localScale.y, scale.z * localScale.z}; };
		glm::vec3	getWorldRotation() const { return rotation + localRotation; };

		//Internally, y cooridnate is flipped.
	private:
		template<class Archive> friend void serialize(Archive&, TransformComponent&);

		glm::vec3 translation = { 0.0f,0.0f,0.0f };
		glm::vec3 scale = { 0.1f, 0.1f, 0.1f };
		glm::vec3 rotation = { 0.f,0.f,0.f };

		glm::vec3 localTranslation = { 0.0f,0.0f,0.0f };
		glm::vec3 localScale = { 1.0f, 1.0f, 1.0f };
		glm::vec3 localRotation = { 0.f,0.f,0.f };

		glm::mat4 cached; // before rendering starts, all transform components cache the transform matrix here.
	};

	struct TransformArrayComponent {
		struct TransformBufferUnit {
			glm::mat4 modelMatrix{ 1.0f };   // already bakes scale (see mat4()); no separate scale needed
		};
		//TransformComponent will by default hold 1 transform value

		//Used to track the number of elements present
		//Also where new data will be placed, overriding existing data
		uint32_t maxIndex = 1;
		glm::mat4 mat4(uint32_t index = 0);
		glm::mat3 normalMatrix(uint32_t index = 0);

		// If using buffer, bufferHandle will store the buffer handle index
		bool usingBuffer = false;
		uint32_t bufferHandle = 0;

		TransformArrayComponent() { resetTransform(); }
		TransformArrayComponent(float x, float y, float z) { resetTransform(); translation[0] = { x,y,z };}
		TransformArrayComponent(glm::vec4& loc) { resetTransform(); translation[0] = glm::vec3(loc);}
		bool useBuffer() const { return usingBuffer; }

		void addTransform(glm::vec3 _translation, glm::vec3 _scale = { -0.1f,-0.1f,-0.1f }, glm::vec3 _rotation = { 0.f,0.f,0.f });
		void setTransform(uint32_t index, glm::vec3 _translation, glm::vec3 _scale = { -0.1f,-0.1f,-0.1f }, glm::vec3 _rotation = { 0.f,0.f,0.f })
		{
			if (index < MAX_TRANSFORM_PER_ENT) {
				_translation.y *= -1;
				translation[index] = _translation;
				scale[index] = _scale;
				rotation[index] = _rotation;
			}
			else throw("index out of bounds: TransformComponent::translation   MAX_TRANSFORM_PER_ENT: " + MAX_TRANSFORM_PER_ENT);
		}
		void resetTransform() {
			translation.fill({ 0.f,0.f,0.f });
			scale.fill({ 0.1f, 0.1f, 0.1f });
			rotation.fill({ 0.f,0.f,0.f });
			localTranslation.fill({ 0.f,0.f,0.f });
			localScale.fill({ 1.0f,1.0f,1.0f });
			localRotation.fill({ 0.f,0.f,0.f });
			maxIndex = 1;
		}
		void removeLastNTransform(uint32_t n = 1) {
			maxIndex = n > maxIndex ? 0 : maxIndex - n;
		}
		void ToBufferComponent(DataBufferComponent& bufferComponent);
		uint32_t count() const { return maxIndex; }

		glm::vec3	getTranslation(uint32_t i) const { return translation[i]; };
		void		setTranslation(uint32_t i, const glm::vec3& _translation) { translation[i] = _translation; };
		glm::vec3	getScale(uint32_t i) const { return scale[i]; }
		void		setScale(uint32_t i, const glm::vec3& _scale) { scale[i] = _scale; };
		glm::vec3	getRotation(uint32_t i) const { return rotation[i]; }
		void		setRotation(uint32_t i, const glm::vec3& _rotation) { rotation[i] = _rotation; };

		glm::vec3	getLocalTranslation(uint32_t i) const { return localTranslation[i]; };
		void		setLocalTranslation(uint32_t i, const glm::vec3& _translation) { localTranslation[i] = _translation; };
		glm::vec3	getLocalScale(uint32_t i) const { return localScale[i]; }
		void		setLocalScale(uint32_t i, const glm::vec3& _scale) { localScale[i] = _scale; };
		glm::vec3	getLocalRotation(uint32_t i) const { return localRotation[i]; }
		void		setLocalRotation(uint32_t i, const glm::vec3& _rotation) { localRotation[i] = _rotation; };

		glm::vec3	getWorldTranslation(uint32_t i) const { return translation[i] + localTranslation[i]; };
		glm::vec3	getWorldScale(uint32_t i) const { return { scale[i].x * localScale[i].x, scale[i].y * localScale[i].y, scale[i].z * localScale[i].z}; };
		glm::vec3	getWorldRotation(uint32_t i) const { return rotation[i] + localRotation[i]; };

	private:
		template<class Archive> friend void serialize(Archive&, TransformArrayComponent&);

		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> translation;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> scale;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> rotation;

		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> localTranslation;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> localScale;
		std::array<glm::vec3, MAX_TRANSFORM_PER_ENT> localRotation;
	};

	struct TransformHierachyComponent {
		entt::entity parent;
		bool hasParent = false;
		uint32_t depth = 0;
		glm::vec3 localTranslation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 localRotation    = { 0.0f, 0.0f, 0.0f };
		glm::vec3 localScale       = { 1.0f, 1.0f, 1.0f };
		// Optional: name of an attach point on the PARENT entity. When set (and the parent has a
		// matching AttachmentComponent point), the child rides that bone-anchored point instead of
		// the parent's root transform. Empty => normal root parenting.
		std::string attachment;
	};
}
