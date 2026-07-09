#pragma once

// Aggregator header. The component structs that used to live here have been split
// into components/*.hpp (one header per concern). This umbrella is kept so existing
// translation units can keep including "bagel_ecs_components.hpp" and pull in the
// whole component set unchanged.
//
//   components/data_buffer.hpp  - DataBufferComponent (GPU buffer wrapper)
//   components/transform.hpp    - Transform / TransformArray / TransformHierachy
//   components/light.hpp        - PointLight / DirectionalLight
//   components/model.hpp        - MaterialSource / Model / Wireframe / CollisionModel
//   components/physics.hpp      - JoltPhysics / JoltKinematic
//   components/tag.hpp          - Info / Transient (tag & marker components)

#include "ecs/components/data_buffer.hpp"
#include "ecs/components/transform.hpp"
#include "ecs/components/light.hpp"
#include "ecs/components/model.hpp"
#include "ecs/components/physics.hpp"
#include "ecs/components/tag.hpp"
