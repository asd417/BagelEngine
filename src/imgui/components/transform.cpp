#include "imgui/components/component_panels.hpp"

#include "ecs/bagel_ecs_components.hpp"
#include "imgui.h"
#include <glm/glm.hpp>

namespace bagel
{
void DrawTransformComponent(entt::registry &registry, entt::entity entity)
{
    auto *t = registry.try_get<TransformComponent>(entity);
    if (!t)
        return;

    glm::vec3 p = t->getTranslation();
    if (ImGui::DragFloat3("Translation", &p.x, 0.05f))
        t->setTranslation(p);
    glm::vec3 r = t->getRotationDegrees();
    if (ImGui::DragFloat3("Rotation", &r.x, 0.05f, -360, 360))
        t->setRotationDegrees(r);
    glm::vec3 s = t->getScale();
    if (ImGui::DragFloat3("Scale", &s.x, 0.01f))
        t->setScale(s);

    glm::vec3 lp = t->getLocalTranslation();
    if (ImGui::DragFloat3("Local Translation", &lp.x, 0.05f))
        t->setLocalTranslation(lp);
    glm::vec3 lr = t->getLocalRotationDegrees();
    if (ImGui::DragFloat3("Local Rotation", &lr.x, 0.05f, -360, 360))
        t->setLocalRotationDegrees(lr);
    glm::vec3 ls = t->getLocalScale();
    if (ImGui::DragFloat3("Local Scale", &ls.x, 0.01f))
        t->setLocalScale(ls);
}

void DrawTransformArrayComponent(entt::registry &registry, entt::entity entity)
{
    if (auto *ta = registry.try_get<TransformArrayComponent>(entity))
        ImGui::Text("TransformArray: %u instances, buffered=%s", ta->count(), ta->useBuffer() ? "yes" : "no");
}

void DrawTransformHierarchyComponent(entt::registry &registry, entt::entity entity)
{
    if (auto *h = registry.try_get<TransformHierachyComponent>(entity))
        ImGui::Text("Hierarchy: parent=%u depth=%u",
                    h->hasParent ? (uint32_t)entt::to_integral(h->parent) : 0u, h->depth);
}
} // namespace bagel
