#pragma once
#include "entt.hpp"

namespace bagel
{
// Per-component sections of the registry inspector (DrawRegistryPanel). Each one guards on
// try_get / all_of internally and draws nothing when the entity lacks the component, so the
// panel can call them unconditionally. All are invoked inside the panel's per-entity PushID
// scope, which keeps their widget IDs unique across entities.
void DrawTransformComponent(entt::registry &registry, entt::entity entity);
void DrawTransformArrayComponent(entt::registry &registry, entt::entity entity);
void DrawTransformHierarchyComponent(entt::registry &registry, entt::entity entity);
void DrawPointLightComponent(entt::registry &registry, entt::entity entity);
void DrawDirectionalLightComponent(entt::registry &registry, entt::entity entity);
void DrawModelComponent(entt::registry &registry, entt::entity entity);
void DrawPlanetComponent(entt::registry &registry, entt::entity entity);
void DrawAnimationComponent(entt::registry &registry, entt::entity entity);
void DrawWireframeComponent(entt::registry &registry, entt::entity entity);
void DrawDataBufferComponent(entt::registry &registry, entt::entity entity);
void DrawJoltPhysicsComponent(entt::registry &registry, entt::entity entity);
void DrawJoltKinematicComponent(entt::registry &registry, entt::entity entity);
void DrawInfoComponent(entt::registry &registry, entt::entity entity);
} // namespace bagel
