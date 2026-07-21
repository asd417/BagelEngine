#include "imgui/components/component_panels.hpp"

#include "ecs/bagel_ecs_components.hpp"
#include "imgui.h"
#include "physics/bagel_jolt.hpp" // BGLJolt singleton for the sleep controls

namespace bagel
{
void DrawJoltPhysicsComponent(entt::registry &registry, entt::entity entity)
{
    if (!registry.all_of<JoltPhysicsComponent>(entity))
        return;
    ImGui::TextUnformatted("JoltPhysics");
    BGLJolt *jolt = BGLJolt::GetInstance();
    ImGui::Text("  state: %s", jolt->IsBodyActive(entity) ? "awake" : "asleep");
    if (ImGui::SmallButton("Wake"))
        jolt->SetComponentActivity(entity, true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Sleep"))
        jolt->SetComponentActivity(entity, false);
}

void DrawJoltKinematicComponent(entt::registry &registry, entt::entity entity)
{
    if (registry.all_of<JoltKinematicComponent>(entity))
        ImGui::TextUnformatted("JoltKinematic");
}
} // namespace bagel
