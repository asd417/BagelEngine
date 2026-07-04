#include "bagel_compute_system.hpp"

namespace bagel
{
    // class for creating planet geometry with compute shader
    class PlanetComputeSystem : public BGLComputeSystem{
        public:
        PlanetComputeSystem(std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry);
        ~PlanetComputeSystem() = default;
        private:
        entt::registry& registry;
    };
}
