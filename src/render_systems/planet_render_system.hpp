#pragma once
#include <vector>

#include "entt.hpp"
#include <glm/glm.hpp>

#include "bagel_frame_info.hpp"
#include "bagel_render_system.hpp"
#include "planet/components/planet.hpp"

namespace bagel
{

// Planets are shaded by an elevation colour gradient (no texture maps), evaluated per-fragment.
// The gradient stops, the mesh's elevation span, and the base radius are all delivered here.
struct PlanetPushConstantData
{
    glm::mat4 modelMatrix{1.0f};
    glm::vec4 params{0.0f};                  // x=radius, y=minElevation, z=maxElevation, w=gradientCount
    // x=sphereness (1 sphere / 0 cube). The vertex shader needs it to divide the cube bulge back
    // out of |position| before keying the gradient — see planet_gbuffer.vert. yzw are spare.
    glm::vec4 params2{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 gradient[MAX_GRADIENT_POINTS]; // per stop: xyz=colour, w=height key in [0,1]
};

// 224 bytes with 8 gradient stops. 256 is the practical ceiling (the Vulkan spec only guarantees
// 128, but this range has always exceeded that; every desktop driver reports 256). Adding another
// vec4 or two gradient stops overflows it — move the gradient to a UBO/SSBO at that point.
static_assert(sizeof(PlanetPushConstantData) <= 256,
              "planet push constant exceeds the 256-byte budget");

// Draws PlanetComponent entities into the deferred gbuffer. Modeled on GBufferRenderSystem and
// reusing the same gbuffer_fill pipeline/shaders and push constant, so the generated planet mesh
// (indexed, with analytic normals) shades identically to any other model. Kept as its own system
// so planets can later diverge to a dedicated terrain pipeline; the standard GBufferRenderSystem
// skips planet entities so they are not drawn twice.
class PlanetRenderSystem : BGLRenderSystem
{
  public:
    PlanetRenderSystem(
        VkRenderPass renderPass,
        std::vector<VkDescriptorSetLayout> setLayouts,
        entt::registry &_registry);

    void renderEntities(FrameInfo &frameInfo);

  private:
    entt::registry &registry;
};

} // namespace bagel
