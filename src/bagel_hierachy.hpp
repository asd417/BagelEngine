#pragma once
#include "bagel_ecs_components.hpp"

#include "entt.hpp"
namespace bagel {
	class HierachySystem {
	public:
		HierachySystem(entt::registry&);
		~HierachySystem() = default;

		void CreateHierachy(entt::entity parent, entt::entity child);
		void ApplyHiarchialChange();
	private:
		entt::registry& registry;
	};
}