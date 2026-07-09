# src/ Abstraction Layers

Snapshot: branch `lego` @ `0e22da2`, 2026-07-09.

Regenerate with `python tools/include_layers.py` (add `--csv` for machine-readable output).
That script is the source of truth for the numbers below; this file is the interpretation.

## Method

Levels are derived from the `#include` graph, not from filenames:

- **depth** = longest internal include chain to a leaf (0 = includes nothing from `src/`).
- **in-degree** = how many `src/` files include this header. High in-degree means many things
  depend on it, which is what "low level" actually means.

Two caveats that matter:

1. **Depth measures header weight, not conceptual level.** `pose_gizmo.hpp` scores depth 2
   because its header is thin (entt + camera); its `.cpp` is an editor tool. Signal, not verdict.
2. **Only `#include` edges are visible.** Runtime coupling through globals —
   `BGLDevice::device()`, `CONSOLE`, `BGLJolt::GetInstance()` — is a real dependency that no
   folder boundary or include analysis will catch.

## Headline

The include graph is a **clean DAG. Zero header cycles**, max depth 13.

The structure is not tangled. The problem is that **50 files sit at `src/` root spanning eight
levels**, while the existing subfolders (`animation/`, `physics/`, `render_systems/`, …) are
already coherent.

## The levels

Depth in `[N]`.

| Level | Files |
|---|---|
| **L0 — Pure / leaf**<br>no internal deps, no VK, no GLFW | `math/bagel_math.hpp` `math/planet_cubemap.hpp` `math/simplex_noise.hpp` `bagel_util` `bagel_engine_config.hpp` `bagel_frustum.hpp` `bagel_connection_graph.hpp` `components/tag.hpp` `components/physics.hpp` `model_loaders/model_load_settings.hpp` `lego/connector_types.hpp` `lego/part_catalog` `lego/baked_collision` `smaa/AreaTex.h` `smaa/SearchTex.h` `animation/bagel_animation` |
| **L1 — Platform** | `bagel_window`[0] (GLFW) · `bagel_keybinds.hpp`[0] |
| **L2 — GPU device** | `bagel_engine_device`[1] → `bagel_buffer`[2] `bagel_engine_swap_chain`[2] `bagel_pipeline`[2] → `bagel_descriptors`[3] |
| **L3 — GPU resources** | `bagel_texture_streamer`[2] `bagel_model_cache`[3] `bagel_textures`[4] `animation/bagel_skin_manager`[4] `bagel_material`[7] `bagel_model`[7] `smaa/smaa_textures`[0] |
| **L4 — Scene / ECS** | `bagel_camera`[1] `components/model`[2] `components/planet`[3] `components/data_buffer`[4] `components/transform`[5] `bagel_ecs_components`[6] `bagel_ecs_serialize`[7] `bagel_hierachy`[7] `bagel_gameobject`[8] |
| **L5 — Asset pipeline** | `lego/ldraw_library`[1] `lego/baked_connectors`[2] `model_loaders/model_sidecar`[3] `model_loaders/bagel_model_loader`[5] `gltf`/`obj`/`generated`[6] `lego/ldraw_model_loader`[6] `map/bagel_map_io`[9] |
| **L6 — Engine subsystems** | `physics/bagel_physics`[7] `physics/bagel_jolt`[8] `bagel_renderer`[8] `bagel_frame_info`[9] `render_systems/bagel_render_system`[10] → 13 concrete render systems[11] |
| **L7 — Editor / tools** | `pose_gizmo`[2] `lego/lego_browser_panel`[3] `bagel_imgui`[9] `keyboard_movement_controller`[9] `render_systems/gizmo_render_system`[11] `bagel_console_commands`[13] |
| **L8 — Application / game** | `lego/connections`[1] `lego/part_system`[3] `application/bagel_application`[12] `my_application`[13] |

Notable: `animation/bagel_animation` is L0 — pure skeleton/pose math, zero Vulkan.

Most-depended-on headers (in-degree): `bagel_engine_device.hpp` 35, `bagel_ecs_components.hpp` 22,
`bagel_frame_info.hpp` 19, `bagel_model.hpp` 19, `bagel_pipeline.hpp` 18, `bagel_util.hpp` 18,
`render_systems/bagel_render_system.hpp` 17, `bagel_imgui.hpp` 10.

## Layer inversions

Four, all in the root files. Ranked by leverage.

### 1. Low-level code depends on the editor UI (for logging)

`bagel_imgui.hpp` is included by `bagel_model.cpp`, `bagel_textures.cpp`, and
`physics/bagel_jolt.cpp` — purely for `CONSOLE->Log` (2, 3, and 9 call sites respectively).
L3/L6 code reaching up to L7.

**Fix:** a `core/log.hpp` interface with the ImGui console registering as a sink. This is the
one that unblocks the others: it's what lets `gpu/` and `resources/` compile without ImGui on
the include path.

### 2. `components/data_buffer.hpp` → `bagel_descriptors.hpp` + `bagel_engine_device.hpp`

Why `components/transform.hpp` is depth 5. The most fundamental component transitively drags
Vulkan descriptor-pool code into every TU that merely wants a position. A component is data;
it should not know what a `VkDescriptorSetLayout` is.

**Fix:** `DataBufferComponent` holds an opaque handle; move `BGLBuffer` ownership out of the
component.

### 3. `bagel_model.hpp` → `bagel_ecs_components.hpp`

A GPU mesh resource (in-degree 19) depends on the entire ECS component set. Backwards — the
ECS should depend on the mesh, not the reverse. This is why `bagel_model.hpp` is depth 7
despite being fundamental.

It also `#include "tiny_gltf.h"` **in the header**, leaking a very large third-party header
into 19 translation units.

### 4. `components/model.hpp` → `bagel_engine_device.hpp`

Vulkan in an ECS component header.

## CMake notes

- The source globs are **per-directory and non-recursive** (`GLOB`, not `GLOB_RECURSE` — the
  recursive variant is commented out at `CMakeLists.txt:70`). Every new folder needs its own
  `file(GLOB ...)` line plus an entry in `add_executable`. That is the real cost of
  reorganizing — small, but not free.
- **`file(GLOB GAME "src/game/*")` points at a directory that does not exist.** Expands empty,
  harmless. Means moving `my_application.*` into `src/game/` costs zero CMake work.
- **`src/compute_systems/` is not globbed and nothing includes it.** Those `.cpp` files are not
  compiled and not referenced — dead on this branch (leftover from the planet feature, same as
  the CPU-side `perlin`/`perlinD` in `math/bagel_math.hpp`, whose only consumer,
  `planet_terrain.hpp::baseHeight`, was deleted in `96cb051` and exists on no branch).
- `src/lego/components/` is not globbed either; headers-only, so it doesn't matter today.

## Suggested layout

Relocation only — no interfaces introduced, no abstraction layer added. The folder boundary is
documentation, not enforcement. This preserves the "reach straight into the engine" stance.
Only the 50 root files move; existing subfolders stay put.

```
src/core/       bagel_util, bagel_engine_config, bagel_frustum,
                bagel_connection_graph, bagel_log.hpp (new)
src/platform/   bagel_window, bagel_keybinds
src/gpu/        bagel_engine_device, bagel_engine_swap_chain,
                bagel_buffer, bagel_pipeline, bagel_descriptors
src/resources/  bagel_textures, bagel_texture_streamer,
                bagel_model, bagel_model_cache, bagel_material
src/scene/      bagel_ecs_components, bagel_ecs_serialize,
                bagel_hierachy, bagel_camera, bagel_gameobject
src/render/     bagel_renderer, bagel_frame_info
src/editor/     bagel_imgui, bagel_console_commands, pose_gizmo,
                keyboard_movement_controller
src/game/       my_application            <- glob already exists
```

**Order matters.** Fix inversions #1 (logging) and #2 (`data_buffer`) *before* moving anything.
Move first and `src/gpu/` still `#include`s `bagel_imgui.hpp` while `src/components/` still
pulls in descriptors — the tangle is relocated and made to *look* resolved. Fix the two edges
first and the folders become honest.

## Open question

`bagel_gameobject.hpp` — 42 lines, one class `BGLGameObject`, in-degree 8, depth 8, sitting
between `bagel_frame_info` and `bagel_model`. Smells vestigial (pre-ECS). Not verified unused.
If it can be deleted, `bagel_frame_info` drops a level and `src/scene/` gets simpler.
