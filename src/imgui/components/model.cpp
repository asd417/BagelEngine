#include "imgui/components/component_panels.hpp"

#include "ecs/bagel_ecs_components.hpp"
#include "imgui.h"

namespace bagel
{
void DrawModelComponent(entt::registry &registry, entt::entity entity)
{
    auto *m = registry.try_get<ModelComponent>(entity);
    if (!m)
        return;
    ImGui::Text("Model: \"%s\"", m->loadSettings.source.c_str());
    if (m->model)
    {
        const Model &g = m->mesh(); // shared, cache-owned geometry
        ImGui::Text("  submeshes=%u  indexCount=%u  vertexCount=%u", g.submeshCount, g.indexCount, g.vertexCount);
    }
    else
    {
        ImGui::Text("  (model not resolved)");
    }
    ImGui::Text("  frustumCull=%s  matSources=%d", m->frustumCull ? "yes" : "no", (int)m->materialCount);
}

void DrawWireframeComponent(entt::registry &registry, entt::entity entity)
{
    auto *w = registry.try_get<WireframeComponent>(entity);
    if (!w)
        return;
    ImGui::Text("Wireframe: \"%s\"", w->loadSettings.source.c_str());
    ImGui::Text("  submeshes=%u  indexCount=%u  vertexCount=%u", w->submeshCount, w->indexCount, w->vertexCount);
    ImGui::Text("  frustumCull=%s", w->frustumCull ? "yes" : "no");
}
} // namespace bagel
