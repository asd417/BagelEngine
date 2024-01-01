#include "bagel_hierachy.hpp"

namespace bagel {
	HierachySystem::HierachySystem(entt::registry& _r) : registry{_r}
	{}

	void HierachySystem::CreateHierachy(entt::entity parent, entt::entity child) {
		TransformHierachyComponent* p = registry.try_get<TransformHierachyComponent>(parent);
		if (p == nullptr) {
			p = &registry.emplace<bagel::TransformHierachyComponent>(parent);
		}

		TransformHierachyComponent* c = registry.try_get<TransformHierachyComponent>(child);
		if (c == nullptr) {
			c = &registry.emplace<bagel::TransformHierachyComponent>(child);
		}

		c->hasParent = true;
		c->parent = parent;
		c->depth = p->depth + 1;
	}

	void HierachySystem::ApplyHiarchialChange()
	{
		registry.sort<TransformHierachyComponent>(
			[&](const TransformHierachyComponent& lhs, const TransformHierachyComponent& rhs) {
			// Sort to put the components with lower depths value at the front
			return lhs.depth < rhs.depth;
			});
		auto view = registry.view<TransformHierachyComponent, TransformComponent>();
		view.use<TransformHierachyComponent>();
		for (auto [entity, hierachyComp, transComp] : view.each()) {
			if (!hierachyComp.hasParent) continue;
			auto& ptc = registry.get<TransformComponent>(hierachyComp.parent);
			glm::vec3 loc = ptc.getTranslation() + ptc.getLocalTranslation();
			glm::vec3 rot = ptc.getRotation() + ptc.getLocalRotation();
			transComp.setTranslation(ptc.mat4() * glm::vec4(transComp.getLocalTranslation(),1.0f));
			transComp.setRotation(rot);
		}
	}
}
