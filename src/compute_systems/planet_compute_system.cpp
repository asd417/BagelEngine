#include "planet_compute_system.hpp"

namespace bagel
{
    struct PlanetComputePush
    {
    };
    PlanetComputeSystem::PlanetComputeSystem(std::vector<VkDescriptorSetLayout> setLayouts,
                                             std::unique_ptr<BGLBindlessDescriptorManager> const &_descriptorManager,
                                             entt::registry &_registry)
        : BGLComputeSystem(setLayouts, sizeof(PlanetComputePush)),
          registry(_registry) {
            createPipeline("/shaders/compute/test.comp.spv");
          };
}
