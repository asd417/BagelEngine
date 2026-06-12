#include "bagel_hierachy.hpp"

namespace bagel {
	HierachySystem::HierachySystem(entt::registry& _r) : registry{_r}
	{}

	void HierachySystem::CreateHierachy(entt::entity parent, entt::entity child) {
		TransformHierachyComponent* p = registry.try_get<TransformHierachyComponent>(parent);
		if (registry.all_of<JoltPhysicsComponent>(child))
		{
			// Dynamic bodies are owned by Jolt in world space; parenting would fight the solver.
			//CONSOLE->Log("Hierachy", "Refused: dynamic physics body cannot be a hierarchy child");
			return;
		}
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
			[](const TransformHierachyComponent& lhs, const TransformHierachyComponent& rhs) {
				return lhs.depth < rhs.depth;
			});

		auto view = registry.view<TransformHierachyComponent, TransformComponent>();
		view.use<TransformHierachyComponent>();

		for (auto [entity, hier, tc] : view.each()) {
			if (!hier.hasParent) continue;
			auto& ptc = registry.get<TransformComponent>(hier.parent);

			// Pure rotation matrix from parent's world rotation (XYZ Euler, no scale/translation)
			const glm::vec3 pr = ptc.getRotation();
			const float c3 = cosf(pr.z), s3 = sinf(pr.z);
			const float c2 = cosf(pr.y), s2 = sinf(pr.y);
			const float c1 = cosf(pr.x), s1 = sinf(pr.x);
			const glm::mat3 parentRot{
				{ c2*c3,           c1*s3 + c3*s1*s2,  s1*s3 - c1*c3*s2 },
				{-c2*s3,           c1*c3 - s1*s2*s3,  c3*s1 + c1*s2*s3 },
				{ s2,             -c2*s1,              c1*c2            }
			};

			const glm::vec3 ps = ptc.getScale();
			tc.setTranslation(ptc.getTranslation() + parentRot * (ps * hier.localTranslation));
			tc.setRotation(pr + hier.localRotation);
			tc.setScale(ps * hier.localScale);
		}
	}
}
