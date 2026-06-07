# Virtual Aquarium — Implementation Plan

## Current Engine State Summary

| Feature | Status |
|---|---|
| PBR (Cook-Torrance BRDF) | Full |
| Forward rendering | Active, primary path |
| Deferred rendering | G-buffers created, composition pass empty |
| Point lights (10 max) | Full |
| Bindless textures | Full |
| Instanced rendering | Full |
| Physics (Jolt) | Full |
| Transparency (general geometry) | Not implemented |
| Vertex animation / time param | Not implemented |
| Volumetric fog | Not implemented |
| Post-processing pipeline | Not implemented |
| Toon/stylized shading | Not implemented |

---

## Architecture Decision: Forward vs. Deferred

The engine has partial deferred infrastructure (G-buffers allocated at 2048×2048, render pass created) but the composition shader (`CompositRenderSystem`) is completely empty.

**Decision: Complete deferred rendering first, then build aquarium features on top.**

Deferred rendering pays off here because the aquarium will have many point lights (caustics light sources, ambient glow from plants, fish bioluminescence). Forward rendering with many lights requires per-object × per-light work; deferred decouples geometry and lighting passes.

The swaying-plant transparency problem (alpha-blended geometry doesn't work in G-buffer) is handled by a forward pass for transparents after the deferred composition — the standard hybrid approach.

---

## Phase 1 — Complete Deferred Rendering Pipeline

### 1.1 G-Buffer Fill Pass
- Create a new render system `DeferredGBufferSystem` to replace `ModelRenderSystem` for opaque geometry.
- Render to the existing `deferredRenderFrameBuffer` (position, normal, albedo, depth attachments in `bagel_renderer.cpp:494–526`).
- Output layout from `simple_shader.frag` refactored to write to 3 MRT outputs instead of single swapchain color.
- New shader: `shaders/gbuffer_fill.vert/.frag`
  - Output 0 (R16G16B16A16_SFLOAT): world-space position (xyz) + metallic (w)
  - Output 1 (R16G16B16A16_SFLOAT): world-space normal (xyz) + roughness (w)
  - Output 2 (R8G8B8A8_UNORM): albedo (rgb) + AO (a)

### 1.2 Deferred Lighting / Composition Pass
- Implement `CompositRenderSystem` (`src/render_systems/composit_render_system.cpp` — currently blank).
- New shader: `shaders/deferred_lighting.vert/.frag`
  - Full-screen triangle (no vertex buffer needed; generate positions in vert shader from `gl_VertexIndex`).
  - Sample G-buffer textures via bindless descriptor handles.
  - Re-run Cook-Torrance BRDF using G-buffer data + GlobalUBO lights.
  - Supports up to 64 lights (expand `MAX_LIGHTS` from 10).
  - Output: HDR scene color to intermediate HDR render target.

### 1.3 Forward Transparency Pass (after deferred)
- Render alpha-blended geometry (plants, fish fins, water particles) in a forward pass after composition.
- Sort transparent entities back-to-front each frame.
- Enable `BGLPipeline::enableAlphaBlending()` (already exists at `bagel_pipeline.cpp:128–139`).

### 1.4 Frame Loop Restructure (`src/first_app.cpp`)
```
beginPrimaryCMD()
  [G-Buffer Pass]     DeferredGBufferSystem → deferredRenderFrameBuffer
  [Lighting Pass]     CompositRenderSystem  → HDR render target
  [Forward Transparent Pass] TransparentRenderSystem → HDR render target
  [Post-Process Pass] PostProcessSystem     → swapchain
  [UI Pass]           ImGui                 → swapchain
endPrimaryCMD()
```

---

## Phase 2 — Aquarium Shaders & Atmosphere

### 2.1 Underwater Volumetric Fog
- Implemented as a **post-process** pass after the deferred composition using the depth buffer.
- Shader: `shaders/underwater_fog.frag`
  - Reconstruct world position from depth + inverse projection matrix.
  - Exponential-squared depth fog: `fog = exp(-(density * depth)^2)`
  - Layer two fog terms: view-distance fog (horizontal) + depth fog (vertical — darker at bottom).
  - Color: deep blue-green tint, e.g. `vec3(0.05, 0.15, 0.25)`.
  - Add animated light shafts: sample a noise texture scrolling upward, multiply by cone from surface.
- Parameters exposed in GlobalUBO or dedicated `FogUBO`:
  - `fogDensity`, `fogColor`, `lightShaftIntensity`, `lightShaftDirection`

### 2.2 Caustics (Light Patterns on Surfaces)
- Caustics are projected light patterns simulating refracted sunlight from the water surface.
- Implementation: additive light pass in deferred composition shader.
  - Sample a tileable caustics texture (can be animated by blending two slightly offset samples).
  - Project caustics in world space (XZ plane) using world-space position from G-buffer.
  - Fade caustics with surface normal dot upward-vector (only flat/upward surfaces receive them).
  - Animate by scrolling UV with `time`.
- Requires: caustics texture asset (procedural or pre-baked).

### 2.3 Toon / Stylized PBR Shading
- Goal: PBR base with cartoon cel-shading aesthetic layered on top.
- In the deferred lighting shader:
  - Quantize diffuse term to N bands: `floor(NdotL * bands) / bands` (e.g. 3–4 bands).
  - Keep specular sharp (high contrast highlight, not smooth).
  - Add rim lighting: `pow(1.0 - NdotV, rimPower) * rimColor` — gives a glowing edge on fish.
  - Silhouette outlines: rendered in a separate geometry pass using backface expansion technique (scale normals outward slightly, render black backfaces).
- Parameters: `toonBands`, `rimColor`, `rimPower`, `outlineThickness`, `outlineColor`.

### 2.4 Water Surface
- Plane mesh at the top of the aquarium tank.
- Vertex shader: sine-wave displacement for surface ripples.
  - `displacement = A * sin(k*x + omega*time) + B * sin(k2*z + omega2*time)`
- Fragment shader: semi-transparent, Fresnel-based reflection/refraction.
  - Look up cubemap or screen-space reflection for the reflection term.
  - Refraction: sample the scene color texture with a slight UV offset (distortion from screen-space).
- Alpha blending: use existing `enableAlphaBlending()` pipeline path.

---

## Phase 3 — Vertex Animation

### 3.1 Pass Time to Shaders
- Add `float time` to `GlobalUBO` (`src/bagel_frame_info.hpp:31`).
- Update `time` each frame in `first_app.cpp` main loop (alongside existing ambient update).
- This single change enables all time-based animation in any shader.

### 3.2 Plant / Seaweed Swaying
- New shader variant: `shaders/plant.vert/.frag`
  - Inherit G-buffer outputs from gbuffer_fill shaders.
  - In vertex shader: apply sine-wave offset to XZ position based on vertex height (Y).
    - `float sway = sin(time * speed + worldPos.y * frequency) * amplitude`
    - `worldPos.xz += sway * swayAxis`
  - Amplitude scales with Y height so the base is anchored and tips sway most.
- New ECS component: `PlantComponent { float swaySpeed; float swayAmplitude; float swayFrequency; }`
- Plant entities use `PlantComponent` + `TransparentModelComponent` (for alpha-blended leaves).

### 3.3 Fish Procedural Swimming Animation
- New shader: `shaders/fish.vert/.frag`
- Body wave animation (carangiform locomotion):
  - Sine wave travels from head to tail along the fish mesh X axis.
  - `float wave = sin(time * tailFrequency + localPos.x * waveNumber) * tailAmplitude`
  - Amplitude envelope: `amp = max(0, (localPos.x - bodyStart) / bodyLength)` — zero at head, max at tail.
  - Displace in Z (left-right): `localPos.z += wave`
- Tail fin: larger amplitude than body, phase-shifted.
- New ECS component: `FishAnimComponent { float tailFrequency; float tailAmplitude; float waveNumber; }`

---

## Phase 4 — Fish AI & Scene

### 4.1 Fish Behavior System
- New ECS system: `FishBehaviorSystem`
- New ECS component: `FishBehaviorComponent`:
  ```cpp
  struct FishBehaviorComponent {
    glm::vec3 velocity;
    glm::vec3 target;
    float speed;
    float turnSpeed;
    float wanderTimer;
    float wanderInterval;   // seconds before picking new target
    glm::vec3 tankMin, tankMax; // aquarium bounds
  };
  ```
- Behavior: random wander within tank bounds.
  - Every `wanderInterval` seconds, pick a random point within `[tankMin, tankMax]`.
  - Steer toward target using simple proportional control on velocity.
  - Keep fish oriented in the direction of movement (update `TransformComponent.rotation`).
  - Boundary avoidance: turn away when within margin of tank walls.
- Optional flocking (Phase 5): Reynolds boids (separation, alignment, cohesion).

### 4.2 Scene Construction (`src/first_app.cpp`)
- Replace `placeCubes()` with `buildAquarium()`:
  - **Tank geometry**: 5 box faces (no top) as static opaque mesh.
  - **Gravel floor**: flat subdivided mesh with displacement mapped texture.
  - **Plants**: 6–10 plant entities at random floor positions, `PlantComponent` attached.
  - **Fish**: 8–12 fish entities, `FishBehaviorComponent` + `FishAnimComponent` attached.
  - **Bubbles**: particle emitters near plants (optional Phase 5).
  - **Point lights**: 4–6 lights: 2 warm "sunlight from above," 2–4 colored ambient glow near plants.
  - **Caustics light**: a directional "caustics" light position in UBO.

### 4.3 Aquarium Camera
- Locked camera looking at tank center, with orbit control (LMB drag).
- Or free-fly inside the tank (already available via `freeFly` console command).

---

## Phase 5 — Post-Processing & Polish

### 5.1 HDR Render Target + Tone Mapping
- Render scene to R16G16B16A16_SFLOAT intermediate texture (not directly to swapchain).
- Post-process shader applies:
  - **Reinhard or ACES** tone mapping.
  - **Exposure** control.
  - **Vignette**: darkened edges for aquarium framing.
  - **Chromatic aberration**: subtle lens effect for underwater look.
  - **Color grading**: blue-green color shift for underwater atmosphere.

### 5.2 Bloom
- Two-pass Gaussian blur on bright regions (luminance > threshold).
- Add blurred result back to HDR scene before tone mapping.
- Makes fish rim lighting and plant emissive glow pop.

### 5.3 Bubble Particles
- CPU-simulated bubble positions, rendered as instanced billboard quads.
- Alpha-blended circles with Fresnel sheen.
- Rise upward with slight drift, reset at plant base when reaching surface.
- Uses existing `TransformArrayComponent` (instanced rendering already supported).

### 5.4 Screen-Space Ambient Occlusion (SSAO)
- Standard SSAO using G-buffer position + normal.
- Multiply AO term into deferred lighting for contact shadows under rocks, plants, fish.
- Low priority — adds subtle depth without being obvious in cartoonish style.

---

## Asset Requirements

| Asset | Format | Notes |
|---|---|---|
| Fish mesh | OBJ | Low-poly, single UV set. 1–2 species. |
| Seaweed / plant mesh | OBJ | Multi-segment, Y-axis aligned for sway shader |
| Rock / coral mesh | OBJ | Static decoration |
| Gravel/sand texture | PNG (PBR set) | Albedo, normal, roughness |
| Caustics texture | PNG | Tileable, animated via UV scroll or texture array |
| Tank glass texture | PNG | Alpha-mapped for transparency |
| Bubble texture | PNG | Alpha circle with specular highlight |

---

## Implementation Order (Priority)

```
Priority 1 (Foundation)
  ├── 1.1 G-Buffer fill pass (new gbuffer_fill shaders)
  ├── 1.2 Deferred lighting composition (CompositRenderSystem)
  └── 3.1 Pass time to shaders (GlobalUBO time field)

Priority 2 (Core Aquarium Look)
  ├── 2.1 Underwater volumetric fog shader
  ├── 2.3 Toon / stylized PBR shading
  └── 1.3 Forward transparency pass

Priority 3 (Animation)
  ├── 3.2 Plant swaying vertex shader
  ├── 3.3 Fish swimming vertex shader
  └── 4.1 Fish behavior (wander AI)

Priority 4 (Scene & Environment)
  ├── 2.2 Caustics light patterns
  ├── 2.4 Water surface mesh + shader
  └── 4.2 Scene construction (aquarium geometry, fish/plant placement)

Priority 5 (Polish)
  ├── 5.1 HDR + tone mapping + color grading
  ├── 5.2 Bloom
  ├── 5.3 Bubble particles
  └── 5.4 SSAO (optional)
```

---

## Key Files to Modify

| File | Change |
|---|---|
| `src/bagel_frame_info.hpp` | Add `time` to GlobalUBO; expand `MAX_LIGHTS` to 64 |
| `src/bagel_renderer.hpp/.cpp` | Add HDR render target; restructure frame loop |
| `src/first_app.cpp` | Replace scene setup; wire new render systems |
| `src/bagel_ecs_components.hpp` | Add `PlantComponent`, `FishAnimComponent`, `FishBehaviorComponent` |
| `shaders/simple_shader.frag` | Refactor outputs to G-buffer MRT layout |
| `shaders/` | Add: `gbuffer_fill`, `deferred_lighting`, `underwater_fog`, `plant`, `fish`, `water_surface`, `toon_outline`, `postprocess` |
| `src/render_systems/composit_render_system.cpp` | Implement deferred composition |
| `src/render_systems/` | Add: `DeferredGBufferSystem`, `TransparentRenderSystem`, `FishBehaviorSystem`, `PostProcessSystem` |
