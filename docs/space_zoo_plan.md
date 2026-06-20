# Stellar Menagerie — Design & Implementation Plan

> A space zoo-simulation game built on **Bagel Engine**.
> Branch: `space-zoo-game`. This document is the source of truth for scope; code follows it.

---

## 1. Concept

You are the caretaker of a **small planet fitted with comically oversized rocket
boosters**. The planet is your zoo. You fly it between star systems, capture exotic
**aliens** from the worlds you visit, build **habitats and attractions** on the planet's
surface, and dock at **civilizations** so their citizens can come aboard as paying
**visitors**. Space is hostile: **meteor showers, solar radiation, and other cosmic
disasters** threaten your animals, guests, and structures, and must be designed around.

The loop, in one line:
**Travel → Collect aliens → Build habitats → Attract visitors → Earn → Upgrade → Survive disasters → Travel farther.**

### Pillars
1. **The planet is the zoo *and* the vehicle.** Everything is built on one curved (or
   for v1, flat-disc) surface that physically moves between systems. Travel and zoo
   management are the same place, not separate screens.
2. **Creative, detailed building.** A flexible placement/grid system is the heart of
   the experience — paths, fences, terrain, habitat props, decorations, life support.
3. **Living alien collection.** Each species has needs (biome, temperature, diet,
   social rules) that the player satisfies through building, not menus.
4. **Survival under cosmic events.** Disasters create stakes and reward defensive design
   (shields, bunkers, radiation shielding, structural integrity).

---

## 2. What the engine already gives us (grounding)

Mapped from the current `src/` so the plan stays realistic. The game is a subclass of
`Application`, mirroring `MyApplication`.

| Need | Existing engine capability | Source file |
|------|---------------------------|-------------|
| Game entry / per-frame hooks | `Application::OnSceneLoad / OnUpdate(dt) / OnDrawGui`, `run()` | `bagel_application.hpp` |
| Entities & data | `entt::registry` ECS | `bagel_application.hpp` |
| Transforms + parenting + bone attach points | `TransformComponent`, `TransformHierachyComponent`, `AttachmentComponent` | `components/transform.hpp`, `components/model.hpp` |
| Save / load whole scene | `.bmap` = versioned registry snapshot (`Map::save/load/unload`) | `map/bagel_map_io.hpp` |
| Console commands (cvar/concommand style) | `ConsoleCommand::*` callbacks taking `Application*` | `bagel_console_commands.hpp` |
| Models (OBJ/GLTF/generated) + PBR materials | `ModelComponent`, `Material`, `MaterialSource`, model loaders + `.yaml` sidecars | `components/model.hpp`, `model_loaders/` |
| **Static instanced rendering** | `GBufferRenderSystem` already instances via `TransformArrayComponent` + a buffered-transform SSBO (`vkCmdDrawIndexed(..., count, ...)`) | `render_systems/gbuffer_render_system.cpp` |
| Procedural mesh generation (primitives) | `GeneratedModelLoader` builds grid/cube/sphere meshes into vertex/index buffers | `model_loaders/generated.*` |
| Skeletal animation + IK (for walking aliens) | `AnimationComponent`, skin-influence + joint-palette SSBOs, `PoseGizmo` | `components/model.hpp`, `animation/`, `pose_gizmo.*` |
| In-world manipulation gizmo | `PoseGizmo` (reusable pattern for a build/placement gizmo) | `pose_gizmo.*` |
| Lighting, bloom, shadows, SMAA, radiosity | deferred PBR render systems | `render_systems/` |
| GPU data buffers (bindless SSBO wrapper) | `DataBufferComponent` / `BGLBindlessDescriptorManager` | `components/data_buffer.hpp` |
| Collision queries / raycasting (build validation) | Jolt as a **query engine** (broadphase, raycast, overlap) — not for dynamics; see §2.6 | `physics/bagel_jolt.*` |

**Implication:** we add *game-layer* ECS components + systems and a `SpaceZooApplication`,
and reuse the map format for both **save games** and **prefab/blueprint** storage. Unlike a
typical scene, this game **does need new engine-level rendering/geometry work** — see §2.5.

### 2.5 Engine capability gaps (net-new rendering/geometry workstreams)

Per the gameplay's actual demands, these three are first-class engineering tracks, not
incidental game code. They are the project's real technical risk (more than the gameplay sim).

**A. Instanced rendering of *animated* meshes — the headline feature.**
Crowds of tourists (and herds of aliens) are many copies of a few skinned models, so they must
be drawn **instanced**, not per-entity.
- *Today:* `SkinnedGBufferRenderSystem` is explicitly **per-entity, not buffered/instanced**
  (one draw + one push-constant per skinned entity). Static instancing exists separately
  (`GBufferRenderSystem` + `TransformArrayComponent`). **Neither covers instanced + skinned.**
- *Build:* a new `InstancedSkinnedGBufferRenderSystem` + `skinned_gbuffer_instanced.vert` that
  merges the two: per-instance data (model matrix + per-instance `animBaseOffset` into the joint
  palette + skin row) lives in an SSBO indexed by `gl_InstanceIndex`, while the existing
  per-vertex skin-influence SSBO and joint-palette SSBO are reused. One `vkCmdDrawIndexed` with
  `instanceCount = N` per model type.
- *Animation variety:* a shared palette holds the baked clip frames; each instance just stores
  its current frame's palette base (its own clip/time/phase), so a crowd animates with offset
  phases from one model. CPU advances per-instance time and writes the SSBO once per frame.
- *Risk:* highest-complexity item. Prototype with a single model + N instances before wiring it
  into gameplay.

**B. Dynamic / editable mesh generation — tourist paths.**
Paths are authored on the planet surface and can be **raised/lowered**, so their geometry is
generated and re-generated at runtime, not loaded from disk.
- *Today:* `GeneratedModelLoader` shows the pattern for writing procedural verts/indices into a
  `ModelComponent`'s buffers, but only for static primitives built once at load.
- *Build:* a `DynamicMeshComponent` + `PathMeshBuilder` that takes a list of control points on
  the sphere, projects them to the surface (with per-segment height offset along the normal for
  raise/lower), and generates a ribbon/strip mesh (with width, walls/supports for raised
  sections). Re-tessellate + re-upload **only on edit**, not per frame. Needs a safe
  buffer-resize/replace path (respect `ModelComponent`'s move-only buffer ownership;
  `vkDeviceWaitIdle` or ring of buffers before freeing in-flight geometry).
- Reused by: barriers/fences (extruded along a path) and possibly biome-terrain deformation later.

**C. Particle system — space effects.**
Meteor showers, radiation shimmer, ion-storm dust, and booster exhaust need many short-lived
sprites. None exists today.
- *Build:* a `ParticleSystemComponent` + emitter (rate, lifetime, velocity, gravity-to-center,
  color/size-over-life) and a `ParticleRenderSystem` drawing **instanced camera-facing billboards**
  (reuses the same per-instance-SSBO instancing approach as A/the static instancer). CPU
  simulation is fine for v1 (thousands of particles); revisit a compute-shader sim only if
  profiling demands it. Additive blending for glows; integrate with the existing bloom pass.

These three share one backbone — **per-instance data in a bindless SSBO** — so build the
instancing foundation once (A) and B/C reuse it.

### Key constraints to respect
- `ModelComponent` is **move-only** (owns Vk buffers) — never copy; transfer or share via the
  loader's dedup path.
- Serialization is **persistent state only**; transient GPU/physics/bindless state is rebuilt
  in a **rehydrate** pass after load (see `MyApplication::rehydrateScene`). Every new
  serialized component needs the same discipline.
- Adding a component to a saved map = add its `serialize` overload + register it in the
  SaveRegistry/LoadRegistry manifest, and **bump `Map::VERSION`** on incompatible changes.
- `TransformComponent` flips Y internally — keep build-grid math in one convention and convert
  at the edges.
- Move-only buffer-owning components (`ModelComponent`, `DataBufferComponent`) must never be
  copied; the dynamic-mesh and instancing work has to honor this when resizing/replacing buffers.

### 2.6 Physics: not a core mechanic, but used for queries

Rigid-body **dynamics** are not a core mechanic, but Jolt is **kept specifically as a query
engine** for the building system:
- **Collision checking & raycasting (primary use):** building validation needs efficient spatial
  queries — cursor→world raycast for placement, and overlap/penetration tests so pieces don't
  intersect existing builds or terrain. Jolt's broadphase + cast/collide queries are the right
  tool; reuse `BGLJolt` rather than hand-rolling a spatial index.
  - Placed pieces register lightweight **collision proxies** (static Jolt bodies from
    `CollisionModelComponent`/simple shapes) used purely for these queries, not for simulation.
  - The cursor→sphere ray can start as analytic sphere math (`PlanetSurface`), but Jolt raycasts
    give precise hits against actual placed geometry/terrain for snapping and "what's under the
    cursor" picking.
- **Agent movement** (tourists, aliens) stays **kinematic/sim-driven** — surface-constrained
  steering + path-following, not rigid-body dynamics.
- **Full dynamics** remain optional flair only (e.g. meteor debris tumbling). Don't let simulation
  gate the core loop, and don't spend effort on engine work that belongs in §2.5.

---

## 3. Game systems

### 3.1 The Planet & Travel
- **Surface model:** **curved spherical planet from v1.** The zoo is built on the surface of a
  small sphere ("tiny planet"). All placement, camera, and gravity are surface-relative:
  - Position on the surface is a point on the sphere; **"up" is the surface normal** (radial
    direction) at that point, and an object's orientation is built from that normal + a tangent
    heading. Centralize this in a `PlanetSurface` helper (`pointToNormal`, `surfaceBasis`,
    `project/raycast onto sphere`) so every system shares one convention.
  - Camera orbits/pans around the sphere; gravity points toward the planet center.
  - **Why this is the hard part:** snapping, footprints, path graphs, and enclosure flood-fill
    all have to work in spherical/tangent space rather than on a plane. Keep that math behind the
    `PlanetSurface` interface so gameplay code stays surface-agnostic. (Build/test it first in M0.)
- **Boosters:** visual + a `BoosterComponent` (thrust level, fuel burn rate). Mostly
  cosmetic/economic in v1 — they gate travel via fuel.
- **Star map:** a separate UI/overlay (`OnDrawGui`) listing reachable star systems, each with:
  distance (fuel cost), available alien species, civilization(s), and disaster risk profile.
- **Travel:** select destination → consume fuel → time-skip / transit sequence → arrive at a
  new system that reshuffles available aliens, visitors, and the disaster table.
- **Resources:** `Fuel`, `Credits`, `Power`, `LifeSupport` (planet-wide sim values held in a
  singleton `PlanetState`).

### 3.2 Alien Collection
- **Species data** is *content*, authored in a data file (JSON/YAML) — not hardcoded:
  - model + animation set, biome (e.g. ice/lava/toxic/verdant), temperature band, diet,
    social rule (solitary/herd), space needed, danger level, visitor appeal, capture difficulty.
- **Capture:** at a system you launch a small "expedition" minigame/abstraction; success adds an
  alien entity to a holding pen. v1 can be a probability roll + cost; v2 a real mechanic.
- **Habitat satisfaction:** a per-animal `Happiness` derived from how well its enclosure matches
  its species needs (biome props nearby, enough space, right temperature device, fed, social
  count). Happiness drives visitor appeal and breeding.
- **Behavior:** aliens are skinned models that wander within their enclosure bounds (reuse
  `AnimationComponent` + IK leg work already in the repo). Pathing v1 = bounded random walk.
- **Rendering:** herds of the same species draw through the **instanced skinned renderer** (§2.5-A),
  so a populated enclosure is one instanced draw, not N per-entity draws.

### 3.3 Civilizations & Visitors
- Each civilization = a faction with **reputation**, a **taste profile** (which species/biomes
  its citizens love), ticket-price tolerance, and unlockable rewards (rare species, building
  parts, booster upgrades).
- **Docking** at a civilization spawns **visitor agents** that walk the paths, view habitats,
  spend credits at attractions, and generate happiness/reputation.
- **Visitor sim (v1, lightweight):** spawn N visitors, each picks habitats weighted by appeal,
  walks there via the path graph, contributes income, then leaves. No deep AI needed initially.
- **Rendering:** crowds are the primary driver of §2.5-A — hundreds of tourists are a handful of
  shared skinned models drawn instanced, each instance phase-offset in its walk cycle. Crowd size
  scales with reputation, so this path must be efficient from the start.

### 3.4 Natural / Cosmic Disasters
Event system driven by the current system's risk table; each event has warning → impact →
aftermath phases so the player can react with design.

| Event | Threat | Player defense (building) |
|-------|--------|---------------------------|
| Meteor shower | Destroys structures/animals in impact zones | Shield generators, reinforced roofs, bunkers |
| Solar radiation | Sickens unshielded animals/visitors | Radiation shielding, indoor enclosures |
| Power surge / blackout | Life support + temperature devices fail | Backup generators, redundancy |
| Cold snap / heat flare | Pushes enclosures out of temp band | Heaters/coolers, insulation |
| Hull/structural stress (from boosting) | Damages fragile builds mid-travel | Structural supports, secure-before-travel |

Implemented as a `DisasterDirector` system that schedules events, applies area effects to
components (health/integrity/happiness), and raises UI warnings. The visible spectacle of each
event — meteor streaks, radiation shimmer, storm dust, booster exhaust — is driven by the
**particle system** (§2.5-C).

### 3.5 Building System (the centerpiece)
A detailed, creative editor that runs **in-game**, reusing engine pieces.

**Placement model — freeform precision (Planet Coaster style)**
- **Smooth free placement + rotation** on the sphere surface, with **optional** snapping rather
  than a hard grid:
  - Objects sit on the surface oriented to the local normal; the player freely rotates around the
    local "up" (surface normal) and slides the piece continuously across the surface.
  - Optional snaps: surface-tangent angle increments, edge/vertex snapping to neighboring pieces,
    and surface-distance grid lines for players who want alignment. Hold a modifier to toggle
    snap on/off.
  - Vertical stacking for multi-floor structures, with height offset along the surface normal.
- **Implication:** placement is a sphere raycast from the cursor → surface point → build a
  transform from `surfaceBasis(point)` + the player's heading angle. No flat-plane grid; the
  gizmo and validation operate in tangent space (see `PlanetSurface`, §3.1).
- **Build categories:**
  - *Terrain/ground:* biome tiles (paint ice/lava/grass), elevation.
  - *Paths:* **dynamically generated** walkways the visitor path graph reads. Authored as control
    points on the surface; the mesh is built by `PathMeshBuilder` (§2.5-B) and supports
    **raise/lower** (height offset along the surface normal, with auto-generated supports/ramps for
    elevated sections). Regenerated on edit, not per frame.
  - *Barriers:* fences/walls/glass (enclosure boundaries — define habitat regions). Likely also
    dynamic-mesh-extruded along a path, sharing the path builder.
  - *Habitat props:* rocks, plants, water, biome-specific decor that feed happiness.
  - *Functional:* heaters/coolers, feeders, shield/radiation generators, power, life support.
  - *Attractions/visitor amenities:* food stalls, benches, viewing platforms (income).
  - *Decoration:* purely cosmetic.
- **Tools:** place, delete, move, rotate, copy, multi-select, undo/redo, blueprint
  (save a selection as a reusable prefab — stored as a mini `.bmap`).
- **Validation:** placement rules (no overlap with existing geometry, paths need connection,
  enclosures need full barriers to count, functional devices need power). Overlap + "what's under
  the cursor" use **Jolt raycast/collide queries** (§2.6). Visual feedback (ghost preview,
  red = invalid).
- **Enclosure detection:** flood-fill / region system that turns a fully-barriered area into a
  named `Enclosure` with measured area, biome composition, temperature, and assigned species —
  the bridge between building and animal happiness.

**Engine reuse**
- Reuse the **gizmo pattern** (`PoseGizmo`, `gizmo_render_system`) for a `BuildGizmo`.
- Every placed object is an **entity** with a `Placeable` component → the existing `.bmap`
  save/load *is* the zoo save system. Blueprints are sub-snapshots.
- Pieces are catalog entries (data file) → spawn a `ModelComponent` via the loader +
  optional `CollisionModelComponent` + functional component(s).

---

## 4. New ECS components (game layer)

All serialized ones follow the rehydrate discipline (§2). Names provisional.

- `PlaceableComponent` — catalog id, category, grid cell/anchor, rotation, footprint, integrity/health.
- `EnclosureComponent` — region cells, biome mix, temperature, area, assigned species, list of contained animals.
- `AlienComponent` — species id, happiness, hunger, health, social state, home enclosure entity.
- `SpeciesDef` (data, not component) — needs/appeal/model; loaded from `assets/species/*.json`.
- `VisitorComponent` — credits, satisfaction, target habitat, path progress, lifetime.
- `BuildingFunctionComponent` — type (heater/cooler/feeder/shield/power/lifesupport), power draw, radius, active.
- `BoosterComponent` — thrust, fuel rate (planet-level, few instances).
- `PlanetState` (singleton/context) — credits, fuel, power, life-support, current system, date.
- `DisasterState` (singleton/context) — active events, timers, risk table for current system.
- `PathNodeComponent` / path graph — connectivity for visitor navigation.

**Rendering / geometry layer (§2.5):**
- `InstancedSkinnedComponent` — per-instance transform + animation phase/clip for a crowd of one
  skinned model; backs the instanced skinned draw (§2.5-A). Per-instance SSBO is transient.
- `DynamicMeshComponent` — runtime-generated geometry (control points + params → verts/indices);
  used by paths and barriers (§2.5-B). Persists the *recipe* (control points/heights), rebuilds
  buffers on rehydrate.
- `ParticleSystemComponent` — emitter params + live particle pool for space effects (§2.5-C);
  emitter config serialized, live particles transient.

---

## 5. Architecture & file plan

```
src/render_systems/                # ENGINE-level additions (§2.5), not game-specific
  instanced_skinned_gbuffer_render_system.*  # A: instanced + skinned deferred pass
  particle_render_system.*                    # C: instanced billboard particles
src/animation/ or src/geometry/
  path_mesh_builder.*              # B: control-points -> surface ribbon mesh (raise/lower)
src/game/
  space_zoo_application.hpp/.cpp   # SpaceZooApplication : Application (the MyApplication analog)
  planet_surface.*                 # sphere math: point<->normal, surfaceBasis, cursor raycast
  build/
    build_system.*                 # placement, grid/snap, ghost preview, validation
    build_gizmo.*                  # move/rotate gizmo (PoseGizmo pattern)
    build_catalog.*                # loads piece catalog data, spawns entities
    blueprint.*                    # save/load selection as mini .bmap
    enclosure_system.*             # region/flood-fill -> EnclosureComponent
  sim/
    alien_system.*                 # needs, happiness, wandering behavior
    visitor_system.*               # spawn, path-follow, spending
    economy_system.*               # credits/fuel/power tick
    disaster_director.*            # event scheduling + area effects
    travel_system.*                # star map, fuel cost, system transitions
  data/
    species_db.*  catalog_db.*  civilization_db.*  star_map.*
  ui/
    star_map_panel.*  build_panel.*  hud_panel.*   # OnDrawGui ImGui panels
  game_components.hpp              # the new ECS components above
assets/
  species/*.json  catalog/*.json  systems/*.json   # authored content
docs/space_zoo_plan.md            # this file
maps/                             # save games + blueprints (.bmap), already gitignored-ish
```

**Console commands** (Source-style, added to `ConsoleCommand`): `give_credits`, `give_fuel`,
`spawn_alien <species>`, `travel <system>`, `trigger <disaster>`, `buildmode <0|1>`,
`save_zoo <name>` / `load_zoo <name>` (wrap `Map::save/load` + rehydrate).

**Update flow** in `SpaceZooApplication::OnUpdate(dt)`: economy tick → disaster director →
alien sim → visitor sim → build-mode input. `OnDrawGui`: HUD + active panel (star map / build).

---

## 6. Milestones (incremental, each independently runnable)

**M0 — Skeleton + spherical surface (1st PR)**
- `SpaceZooApplication` subclass wired into the build; **spherical planet mesh + skybox** with a
  surface-orbiting camera and center-pointing gravity. Implement and unit-feel-test the
  `PlanetSurface` helper (point↔normal, `surfaceBasis`, cursor→sphere raycast) — everything else
  depends on it. HUD shows placeholder resources. Loads/saves an empty zoo `.bmap`.

**M1 — Building core (freeform placement)**
- Build mode toggle, catalog of ~5 pieces (rock, fence, heater, decoration, prop), **freeform
  surface placement** via cursor→sphere raycast + `surfaceBasis`, ghost preview with optional
  snapping, place/delete, save/load round-trips the build via `.bmap`.

**M2 — Build gizmo & editing**
- Move/rotate/copy/multi-select, undo/redo, blueprint save/load.

**E1 — Dynamic path mesh (§2.5-B)** *(can run alongside M1/M2)*
- `PathMeshBuilder` + `DynamicMeshComponent`: author control points on the surface, generate the
  ribbon mesh, raise/lower with supports, regenerate-on-edit, persist the recipe in the map.
  Paths feed the visitor path graph (M4).

**E2 — Instanced skinned rendering (§2.5-A)** *(prereq for M3/M4 crowds)*
- `InstancedSkinnedGBufferRenderSystem` + instanced skinned vertex shader; prototype with one
  model × N phase-offset instances, then expose via `InstancedSkinnedComponent`. **Highest-risk
  item — prototype early, in parallel with building.**

**M3 — Enclosures & aliens** *(uses E2)*
- Flood-fill enclosure detection (in tangent space on the sphere), 2–3 species data files,
  capture-by-cost, aliens spawn and wander as instanced skinned herds, happiness from enclosure match.

**M4 — Visitors & economy** *(uses E1 + E2)*
- Path graph over dynamic paths, instanced-skinned visitor crowds spawn + path-follow + spend,
  ticket income, fuel/credits loop.

**M5 — Travel**
- Star map UI, fuel-gated system travel, per-system species/civilization/disaster reshuffle.

**E3 — Particle system (§2.5-C)** *(prereq for M6 spectacle)*
- `ParticleSystemComponent` + `ParticleRenderSystem` (instanced billboards, additive, bloom-aware):
  meteor streaks, radiation shimmer, booster exhaust.

**M6 — Disasters** *(uses E3)*
- Disaster director with meteor + radiation events, particle spectacle, warning UI, defensive
  buildings that mitigate.

**M7 — Polish**
- Civilization reputation/rewards, more content, balancing, audio/vfx, save-slot UX.

---

## 7. Decisions & open questions

**Decided (2026-06-20):**
1. **Surface shape** — ✅ **Curved sphere from v1.** Build on a spherical "tiny planet"; all
   placement/camera/gravity is surface-relative via `PlanetSurface` (§3.1).
2. **Building feel** — ✅ **Freeform precision (Planet Coaster style)** with optional snapping,
   in tangent space on the sphere (§3.5).
3. **Capture depth** — ✅ **Abstract cost/roll for v1**; deepen into an expedition mechanic later.

**Still open (confirm before/at M1):**
4. **Art pipeline** — author placeholder primitives/generated models first, or source GLTF assets early?
5. **Scope of "planet moves"** — full transit sequence with the planet visibly flying, or a
   star-map fast-travel abstraction for v1?
6. **Camera scheme** — fully free orbit around the sphere, or a constrained "over-the-shoulder of
   the surface" cam? (Freeform building on a sphere makes camera control a usability risk.)

---

## 8. Risks
- **Engine rendering/geometry work is the dominant risk** (§2.5), ahead of the gameplay sim. In
  order: (A) instanced *skinned* rendering is net-new and the headline feature — prototype it
  standalone (E2) before crowds depend on it; (B) editable dynamic path meshes need a safe
  buffer-rebuild path respecting move-only buffer ownership; (C) the particle system is fully
  net-new. All three share the per-instance-SSBO backbone — build it once.
- **Spherical surface + freeform building compound each other** — the two hardest design choices
  both got picked, so tangent-space placement, snapping, path graphs, and enclosure flood-fill are
  all non-trivial. Mitigation: build and prove `PlanetSurface` in M0 before any gameplay leans on
  it, and keep every system surface-agnostic by going through that one helper.
- **Building system is large** — it is a primary risk and the main value. Time-box M1/M2 and keep
  the catalog data-driven so content scales without code.
- **Serialization churn** — every new saved component bumps map version; batch component additions
  per milestone to limit version thrash, and keep rehydrate centralized. Dynamic meshes/particles
  persist their *recipe*, never raw GPU buffers.
- **Sim performance** — visitors + aliens + per-frame happiness could grow; budget with simple v1
  algorithms and revisit only if profiling (`showProfile`) flags it.
