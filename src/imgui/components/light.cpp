#include "imgui/components/component_panels.hpp"

#include "ecs/bagel_ecs_components.hpp"
#include "imgui.h"

namespace bagel
{
void DrawPointLightComponent(entt::registry &registry, entt::entity entity)
{
    auto *pl = registry.try_get<PointLightComponent>(entity);
    if (!pl)
        return;
    ImGui::ColorEdit3("PointLight color", &pl->color.x);
    ImGui::DragFloat("PointLight lux", &pl->lux, 5.0f, 0.0f, 50000.0f);
}

void DrawDirectionalLightComponent(entt::registry &registry, entt::entity entity)
{
    auto *dl = registry.try_get<DirectionalLightComponent>(entity);
    if (!dl)
        return;
    ImGui::ColorEdit3("Sun color", &dl->color.x);
    ImGui::DragFloat3("Sun rotation", &dl->rotation.x, 0.5f);
    ImGui::DragFloat("Sun lux", &dl->lux, 10.0f, 0.0f, 100000.0f);
}
} // namespace bagel
