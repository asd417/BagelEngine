#pragma once
#include "bagel_ecs_components.hpp"

#include <glm/glm.hpp>
#include <string>

#include "entt.hpp"
namespace bagel {

	// ---- attachment query API (Source $attachment analog) -----------------------------------
	// Resolve a named attach point on `entity` (it must carry an AttachmentComponent). Mirrors
	// Source's LookupAttachment / GetAttachment. The world transform is
	//   entityWorld * boneGlobal * localOffset
	// where boneGlobal comes from AnimationComponent::currentGlobals (resolved each frame BEFORE
	// the hierarchy pass), or identity if the entity has no skeleton.
	int  lookupAttachment(entt::registry& registry, entt::entity entity, const std::string& name);
	bool getAttachmentWorld(entt::registry& registry, entt::entity entity, int index, glm::mat4& outWorld);

	class HierachySystem {
	public:
		HierachySystem(entt::registry&);
		~HierachySystem() = default;

		// Parent `child` to `parent`. If `attachment` is non-empty, the child rides that named
		// attach point on the parent (resolved each frame) instead of the parent's root transform.
		void CreateHierachy(entt::entity parent, entt::entity child, const std::string& attachment = "");
		// Resolve every skeleton's bone globals for this frame. MUST run before ApplyHiarchialChange
		// ("resolve bones before parents") so attachment-parented children read current bone poses.
		void ResolveSkeletonGlobals();
		void ApplyHiarchialChange();
	private:
		entt::registry& registry;
	};
}