#pragma once

// Aggregator header. The component structs that used to live here have been split
// into components/*.hpp (one header per concern). This umbrella is kept so existing
// translation units can keep including "bagel_ecs_components.hpp" and pull in the
// whole component set unchanged.
//
//   components/data_buffer.hpp  - DataBufferComponent (GPU buffer wrapper)
//   components/transform.hpp    - Transform / TransformArray / TransformHierachy
//   components/light.hpp        - PointLight / DirectionalLight
//   components/model.hpp        - Material / MaterialSource / Model / Wireframe / CollisionModel
//   components/physics.hpp      - JoltPhysics / JoltKinematic
//   components/tag.hpp          - Info / Transient (tag & marker components)

#include "components/data_buffer.hpp"
#include "components/transform.hpp"
#include "components/light.hpp"
#include "components/model.hpp"
#include "components/physics.hpp"
#include "components/tag.hpp"
