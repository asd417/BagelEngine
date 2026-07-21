#include "imgui/components/component_panels.hpp"

#include "ecs/bagel_ecs_components.hpp"
#include "imgui.h"

namespace bagel
{
void DrawDataBufferComponent(entt::registry &registry, entt::entity entity)
{
    if (auto *db = registry.try_get<DataBufferComponent>(entity))
        ImGui::Text("DataBuffer: handle=%u", db->getBufferHandle());
}

void DrawInfoComponent(entt::registry &registry, entt::entity entity)
{
    if (registry.all_of<InfoComponent>(entity))
        ImGui::TextUnformatted("Info");
}
} // namespace bagel
