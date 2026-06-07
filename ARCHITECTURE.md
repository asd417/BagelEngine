    # BagelEngine Architecture

## Core Vulkan Layer

### BGLWindow (`src/bagel_window.hpp`)
Thin GLFW wrapper. Owns the `GLFWwindow*` and a resize flag. Only responsibility is creating the Vulkan surface and reporting resize events to the renderer.

### BGLDevice (`src/bagel_engine_device.hpp`)
The Vulkan instance, physical device, logical device (static), command pool, and queues live here. Global utility hub: creates buffers, allocates memory, does image layout transitions, and runs immediate GPU uploads via `ImmediateUploadContext`. Almost every other class depends on it.

### BGLSwapChain (`src/bagel_engine_swap_chain.hpp`)
Manages swapchain images, depth buffers, framebuffers, and synchronization primitives (semaphores/fences). Defines `MAX_FRAMES_IN_FLIGHT = 2`. Holds a `shared_ptr` to the previous swapchain to support seamless recreation on window resize.

### BGLPipeline (`src/bagel_pipeline.hpp`)
Loads SPIR-V shader files, creates `VkShaderModule`s, and assembles a `VkPipeline` from a `PipelineConfigInfo` struct. One pipeline instance per render system.

---

## Memory & Resource Management

### BGLBuffer (`src/bagel_buffer.hpp`)
Generic GPU buffer (vertex, index, UBO, SSBO). Wraps `VkBuffer` + `VkDeviceMemory` with map/unmap/flush helpers and descriptor info generation. Every system that needs GPU-resident data owns one or more of these.

### BGLTexture / TextureComponentBuilder (`src/bagel_textures.hpp`)
`BGLTexture` does the raw Vulkan work: creates `VkImage`, `VkImageView`, `VkSampler`, manages mipmaps via KTX or STB image loading. `TextureComponentBuilder` orchestrates loading a texture from disk and registering it into the bindless descriptor manager.

---

## Bindless Descriptor System

### BGLBindlessDescriptorManager (`src/bagel_descriptors.hpp`)
The heart of the rendering architecture. Owns a flat array of all GPU textures (`TexturePackage`) and all buffers, each identified by an integer handle. Every render system gets a single descriptor set per frame that exposes the entire resource table — shaders index into it directly by handle. Tracks `isOwned` per texture slot to avoid double-freeing externally-managed resources (e.g. offscreen render targets).

### BGLDescriptorSetLayout / BGLDescriptorPool / BGLDescriptorWriter (`src/bagel_descriptors.hpp`)
Supporting trio used internally by `BGLBindlessDescriptorManager`. Layout declares binding slots; Pool allocates sets from Vulkan; Writer builds `VkWriteDescriptorSet` calls.

---

## Model Loading & ECS Components

### ModelComponentBuilder (`src/bgl_model.hpp`)
Loads OBJ and GLTF files (via tinyobjloader and tinygltf). Generates vertex data including positions, normals, tangents, bitangents, and 10 texture-map indices per vertex. Deduplicates vertices and caches loaded models by name so multiple entities can share the same vertex/index buffers. Also generates wireframe geometry from normal vectors via `getNormalDataAsWireframe`.

### BGLModel::Vertex (`src/bgl_model.hpp`)
The vertex format. Carries position, color, normal, tangent, bitangent, UV, and 10 integer indices into the bindless texture table for PBR materials.

### ECS Components (`src/bagel_ecs_components.hpp`)
All game state lives as plain components on EnTT entities:

| Component | What it holds |
|---|---|
| `TransformComponent` | World/local translation, rotation, scale, normal matrix |
| `TransformArrayComponent` | Up to 1000 transforms for instanced rendering, GPU buffer handle |
| `ModelComponent` | Vertex/index buffers, submesh list with per-submesh texture handles |
| `WireframeComponent` | Inherits ModelComponent, adds wire color |
| `PointLightComponent` | RGBA color, radius |
| `JoltPhysicsComponent` | Jolt `BodyID` linking entity to physics simulation |
| `JoltKinematicComponent` | Kinematic body variant with move mode |
| `DataBufferComponent` | Generic GPU buffer owned by entity |
| `TransformHierachyComponent` | Parent entity reference for hierarchical transforms |

---

## Render Pipeline

### BGLRenderer (`src/bagel_renderer.hpp`)
The frame orchestrator. Owns the swapchain, per-frame command buffers, the `OffscreenPass` (single color+depth attachment for forward pass), and the deferred `FrameBuffer` (position/normal/albedo/depth G-buffers). Manages the full frame lifecycle: `beginPrimaryCMD` → render passes → `endPrimaryCMD`, and triggers swapchain recreation on `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR`.

### BGLRenderSystem (`src/render_systems/bagel_render_system.hpp`)
Abstract base class for all render systems. Owns a `BGLPipeline` and `VkPipelineLayout`. Subclasses call `createPipeline()` in their constructor.

| Render System | File | Role |
|---|---|---|
| `ModelRenderSystem` | `ecs_model_render_system.hpp` | Main ECS-driven PBR pass. Iterates all `ModelComponent` entities, binds vertex/index buffers, issues draw calls with push constants (model matrix, transform buffer handle). |
| `WireframeRenderSystem` | `wireframe_render_system.hpp` | Draws `WireframeComponent` entities as lines for normals visualization and collision outlines. |
| `PointLightSystem` | `point_light_render_system.hpp` | Deferred lighting pass. No vertex buffer (binding/attribute descriptions cleared). Iterates `PointLightComponent` entities, updates `GlobalUBO`, draws with alpha blending. |
| `CompositRenderSystem` | `composit_render_system.hpp` | Composition pass — combines G-buffer attachments into the final swapchain image. (In progress) |
| `SimpleRenderSystem` | `simple_render_system.hpp` | Legacy forward renderer using `BGLGameObject`. Being superseded by the ECS path. |

---

## Frame Data

### FrameInfo (`src/bagel_frame_info.hpp`)
POD struct passed to every render system each frame: delta time, the active command buffer, camera matrices, the bindless descriptor set, and a reference to the EnTT registry.

### GlobalUBO (`src/bagel_frame_info.hpp`)
Shader-side uniform block: projection/view matrices, ambient light, and an array of up to 10 point lights. Written by `PointLightSystem` each frame.

### BGLCamera (`src/bgl_camera.hpp`)
Computes perspective/orthographic projection and view matrices from position and rotation. Provides the inverse-view matrix for world-space reconstruction in shaders.

---

## Physics

### BGLJolt (`src/physics/bagel_jolt.hpp`)
Singleton wrapping Jolt Physics. Owns the `JPH::PhysicsSystem`, thread pool, body interface, and contact/activation listeners. Creates rigid bodies (sphere, box) and links them to EnTT entities via `JoltPhysicsComponent`. Each frame it steps the simulation and applies kinematic body transforms back to `TransformComponent`.

---

## Application & UI

### FirstApp (`src/first_app.hpp`)
Top-level application class. Owns everything: window, device, renderer, global descriptor pool, descriptor manager, the EnTT registry, and all render systems. `run()` is the game loop. `loadECSObjects()` spawns entities, loads models, and sets up physics. Also initializes ImGui via `initImgui()`.

**Member declaration order (= reverse destruction order):**
1. `BGLWindow bglWindow`
2. `BGLDevice bglDevice`
3. `BGLRenderer bglRenderer`
4. `globalPool`, `descriptorManager`

### ConsoleApp (`src/bagel_imgui.hpp`)
ImGui-based in-game console. Singleton with a command dispatch map. `FirstApp` registers commands such as `toggleWireframe`, `togglePhysics`, and `freeFly`.

---

## Utilities

- **`bagel_util.hpp`** — `enginePath()` / `enginePathString()`: prepend `ENGINE_BASE_PATH` (set at build time via CMake) to asset-relative paths.
- **`bagel_math.hpp`** — `GetLookVector()`: quaternion-based look direction helper.
- **`KeyboardMovementController`** — GLFW key state to camera movement translation.

---

## Key Architectural Patterns

**Bindless rendering** — One descriptor set per frame exposes all textures and buffers. Shaders receive integer handles via push constants and index the resource table directly. No per-draw descriptor binding overhead.

**ECS (EnTT)** — All game state is components on entities. Render systems iterate typed component views, not object hierarchies. Components own their GPU resources directly (vertex buffers, texture handles).

**Deferred rendering** — G-buffer pass (position/normal/albedo) → lighting pass (`PointLightSystem`) → composition pass (`CompositRenderSystem`). The offscreen pass result is registered in the bindless table with `isOwned=false` so the descriptor manager does not free it on shutdown.

**Resource deduplication** — `ModelComponentBuilder` caches loaded models by name. Multiple entities can share the same `VkBuffer` via a pointer-to-original stored in `ModelComponent`.

**Double-buffered frames** — `MAX_FRAMES_IN_FLIGHT = 2`. Each in-flight frame has its own command buffer and synchronization primitives to keep the GPU fed without stalling.
