#pragma once
#include "bgl_gameobject.hpp"

namespace bagel {
    BGLGameObject BGLGameObject::makePointLight(float intensity, float radius, glm::vec3 color) {
        auto obj = createGameObject();
        obj.color = color;
        obj.transform.scale.x = radius;
        obj.pointLight = std::make_unique<PointLightComponent>();
        obj.pointLight->color = glm::vec4(color,intensity);
        return obj;
    }
}