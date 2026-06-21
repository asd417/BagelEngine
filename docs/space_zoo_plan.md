# Stellar Menagerie — Design & Implementation Plan

> A space zoo-simulation game built on **Bagel Engine**.
> Branch: `space-zoo-game`. This document is the source of truth for scope; code follows it.
>
> **Aesthetic — cartoony Atompunk / Raygun Gothic, reimagined with PBR.** 1950s–60s optimistic
> retro-sci-fi — chrome and fins, Googie curves, ray-gun silhouettes, glass domes, a bright
> pastel-and-chrome palette with neon/emissive accents — rendered through the engine's stylized PBR +
> atmosphere + bloom over deliberately cartoony forms. Detailed static builds, low-poly instanced
> crowds. The look *is* the identity (full art bible: §3.7).
>
> **Core loop & tone:** alien variety × zoo building × zoo profiting, wrapped in **comedic satire of
> capitalism & propaganda** — everything else serves that loop (design north star: §1 below).

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

> These four are the **thematic** pillars (what the game *is*). The **engagement** pillars — the
> retention hooks each system must actually deliver (why it's *fun*, not merely present) — are in
> **§9**. Every feature decision should pass §9's litmus test: **is this a decision or a chore?**

### Design north star (the lens for every other system)

The pillars say what the game *is*; this says what it *prioritises*. The irreducible game is a **zoo
tycoon loop** with three load-bearing experiences — **and nothing is allowed to compete with them:**
1. **Alien variety** — a broad, characterful collection (§3.2).
2. **Zoo building** — the creative, detailed editor (§3.5).
3. **Zoo profiting** — an economy that rewards good zoos (§3.3).

**Every other system exists *solely* to enhance that loop, never as an end in itself.** Travel
(§3.1), disasters (§3.4), discovery (§3.2), sentiment/ethics (§3.3), progression (§3.6) are all
**feeders and amplifiers** for variety / building / profit, and are kept **as simple as that role
allows**. This is *why* species-gaining is a **chance-based pull** (§3.2) and not a deep sub-game: it
feeds *variety* without stealing attention from building and profit. The top-level litmus (above
§9's "decision or chore"): **does this make the building, the profiting, or the collection better? If
not, cut it — or simplify it until it does.** When a supporting system grows complex, that's the
warning sign.

**Tone — satire of capitalism & propaganda.** The throughline is **comedic satire**: the zoo as a
profit machine that will cage sentient beings, paper over cruelty with **advertising/propaganda**
(orbital billboards that literally *suppress public sentiment*, §3.3), do its **dirty work where no
one is watching** (the uninhabited-system misdeed discount + leak risk, §3.3), and **"advertise
harder"** rather than treat its animals better. The satire is **expressed through the economic
systems themselves** — sentiment, ads, the ethics/exploitation-for-profit dial — not bolted on as
flavour text; the cartoony Atompunk look (§3.7) keeps it light while the mechanics quietly make the
player complicit in the joke.

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

Per the gameplay's actual demands, these are first-class engineering tracks, not incidental game
code — collectively the project's real technical risk (more than the gameplay sim):
**A** instanced skinned crowds · **B** dynamic editable path meshes · **C** particle system ·
**D** planet-horizon culling · **E** atmosphere shader · **F** procedural deep-space skybox ·
**G** geodesic-CDLOD planet terrain (the editable surface itself) ·
**H** orbital-mechanics simulation (the planet *flies*).
A–C share a **per-instance bindless-SSBO** backbone; E/F/atmosphere share the **full-screen-pass**
plumbing already used by the composite/SMAA systems. Build each backbone once and reuse. **G is the
foundation** — it *is* the planet the whole game sits on, so it lands in M0 (§6) before the rest.

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

*LOD note (PA-scale zoom):* because the camera zooms from surface level to whole-planet (§3.1),
buildings and crowds need **level-of-detail / impostors** at distance — swap skinned crowd
instances for cheap billboard impostors and structures for low-poly/merged proxies when small on
screen. Fold this into the instanced path (A) and particle billboards (C) rather than building a
separate LOD system; the horizon cull (D) already removes the far side first.

**D. Planet-horizon culling — a tiny-planet advantage to exploit.**
Because the whole world wraps a sphere, the planet body **occludes everything on its far side**, so
visibility is far cheaper to compute than in a flat world — and this is *occlusion* culling, which
the engine's frustum test (`Frustum::testAABB`) does **not** provide.
- *Test:* for planet center `C`, radius `R`, camera at `P` with `d = |P - C|`, a surface point `S`
  is over the horizon (cull it) when `dot(normalize(S - C), normalize(P - C)) < R / d`. One
  dot-product per object — cheaper than a frustum AABB test, and it removes the entire back
  hemisphere that frustum culling would still process. Add a small bias for object height/radius so
  tall things near the limb aren't clipped early. Lives in `PlanetSurface` (e.g.
  `visibleFromCamera(point, height)`).
- *Synergy with A:* the per-instance SSBO is rebuilt on the CPU each frame anyway, so apply the
  horizon test there to **compact crowds to the visible hemisphere before upload** — fewer
  instances drawn *and* a smaller buffer, roughly halving crowd cost for free. Combine with frustum
  culling (horizon first — it's cheaper and kills more) for the final visible set.
- *Caveat:* it's a surface-occlusion heuristic, not a depth test — it won't cull objects hidden
  behind *near-side* hills/buildings (leave that to the existing depth buffer). Tall structures and
  raised paths on the limb need the height bias to avoid popping.

**E. Atmosphere — a custom shader pass.**
The iconic tiny-planet look (a glowing atmospheric rim around the limb, sky color from the surface)
needs **atmospheric scattering**, which no existing pass produces — it's a new custom shader.
- *Where it slots in:* a new full-screen `AtmosphereRenderSystem` following the existing
  `CompositRenderSystem` / SMAA pattern (full-screen triangle, `BGLRenderSystem` base, bindless
  G-buffer handles + push constants). Run it **after deferred lighting, before bloom**, so the rim
  glow feeds the bloom pass and reads the same depth/position G-buffer the composite already uses.
  No new render-target plumbing — reuse the bindless-handle convention in `CompositionPush`.
- *Shader (`atmosphere.frag`):* analytic single-scattering (O'Neil/Bruneton-style Rayleigh + Mie)
  parameterized by planet center/radius, atmosphere shell radius, sun direction, and scattering
  coefficients. For each pixel, march/integrate the view ray against the atmosphere shell; where the
  G-buffer has surface depth, scatter up to the surface (aerial perspective), and where it's empty
  space, render the sky/limb glow. Can start with a cheap analytic approximation and deepen later.
- *Gameplay synergy:* scattering params are **per star system** (different planets/skies as you
  travel, §3.1) and can be driven by events — e.g. solar radiation tints the sky, an ion storm
  thickens haze (ties to the disaster director, §3.4).
- *Reuse:* shares `ubo.glsl` (camera/sun) and the full-screen-pass plumbing; the sun already exists
  as the directional light (`updateDirectionalUBO`).

**F. Procedural skybox / deep-space backdrop — a shader.**
The space backdrop — starfields, distant galaxies, and nearby planets/moons of the current
system — is rendered **entirely in a shader** rather than from cubemap textures.
- *Where it slots in:* a full-screen `SkyboxRenderSystem`, same full-screen-pass plumbing as E,
  drawn **first into empty-space pixels** (where the G-buffer has no surface / depth = far). It's
  the deep-space layer; the atmosphere pass (E) then composites the planet's rim glow *over* it,
  and bloom runs after both so stars, galaxies, and glow all bloom. Order: **skybox → atmosphere →
  bloom → composite.**
- *Shader (`skybox.frag`):* reconstruct the per-pixel view-ray direction from the inverse
  view-projection, then evaluate procedurally:
  - **Stars:** hash/noise-based point field with twinkle and color/temperature variation.
  - **Galaxies / nebulae:** layered fractal noise bands (the Milky Way streak, colored nebula clouds).
  - **Nearby celestial bodies:** the current system's planets/moons as analytic spheres or
    billboards "at infinity" (lit by the same sun direction), including the **destination planet
    that visibly grows as you approach** during transit.
- *Gameplay synergy:* the sky is **data-driven per star system** (which bodies/galaxy are visible),
  so travel (§3.1, M5) genuinely changes the view — and the destination looming larger is the
  "planet visibly flying" payoff (leans open question §7.8 toward the cinematic option, cheaply).
- *Why shader, not cubemap:* infinite resolution, zero texture memory/streaming, trivially
  animated/parameterized per system, and the moving destination planet can't be a static cubemap.
  Reuses `ubo.glsl` and the full-screen plumbing shared with E.

**G. Geodesic-CDLOD planet terrain — the editable surface itself.**
The planet is **not** a static mesh or a heightmap texture: it's a **subdivided icosphere whose
triangle tree is the authoritative terrain data**, rendered at continuous distance-dependent LOD.
This is the foundation everything else (building, paths, enclosures, horizon cull) sits on, so it
ships in M0. Decided model (§7-9):
- *Data model — sparse triangular quadtree.* 20 base icosahedron triangles, each the root of a
  quadtree via the existing 1→4 midpoint split (`generated.cpp` `getMidpoint`). A node holds 4
  children (3 corner + 1 centre). **Vertices are pooled and shared** (the midpoint cache dedups
  them by edge), so a vertex touched by several triangles is stored once.
- *Height = per-vertex radius* (`distance from centre`). Default = planet radius; an edit just
  changes that float. Because adjacent triangles share the midpoint vertex, **editing height is
  continuous across triangle boundaries for free** — the reason this beats per-face heightmap
  textures. A new midpoint vertex is born at the **average radius of its two parent endpoints**
  (smooth until edited).
- *Sparse / lazy materialisation.* A node allocates its 4 children only when (a) it's inside the
  camera subdivision range **or** (b) it carries an edit needing that resolution. Everywhere else
  the surface stays coarse/procedural and costs nothing. Full subdivision would be `20·4^level`
  triangles (level 8 ≈ 1.3M) — sparse keeps memory bounded and allows deep local detail.
- *Rendering = a "cut" through the tree.* Each frame, traverse and select the leaf frontier (LOD
  cut) by camera distance / screen-space error, gather those triangles into an index buffer, draw.
  Fine triangles below the cut still exist in data; they're just not in this frame's indices.
- *Crack-free via edge-vertex snapping (decided).* Adjacent triangles at different levels create
  T-junction cracks (worsened by radial displacement). **The fix: a fine triangle's edge midpoint
  vertices snap to the coarser neighbour's edge** along any boundary where the neighbour is one
  level coarser — i.e. the extra midpoint is placed at the **linear (Cartesian) midpoint of the two
  coarse endpoints' positions**, i.e. on the straight chord the coarse triangle actually renders —
  NOT re-projected onto the sphere arc (that is where the midpoint already sits, and closes nothing).
  Per rendered edge, if the neighbour across it is coarser, move that edge's midpoint to
  `0.5*(pa.pos+pb.pos)`; otherwise leave it. Requires **explicit edge→neighbour adjacency + each leaf's neighbour LOD levels** built at
  subdivision time (extend the `(min,max)`-keyed midpoint cache to record the two triangles sharing
  each edge), including across the 20 base-face seams and the 12 valence-5 icosahedron corners.
  - *Keep it a single-step snap:* enforce a **restricted/balanced tree** (adjacent leaves differ by
    ≤1 level) so a coarse edge only ever has to absorb *one* extra midpoint — without this, a fine
    patch next to a very coarse one needs a whole fan of snapped verts and the bookkeeping explodes.
    Force-subdivide the coarser neighbour when the level gap would exceed 1.
  - *Cost vs. morph:* snapping removes the crack geometrically but the LOD flip still **pops** (no
    smooth transition) and the snapped edge loses its fine height detail *while collapsed*. Both are
    acceptable for v1; a CDLOD distance-morph (store real + parent position, blend in the vertex
    shader) is the later upgrade that also kills the pop, reusing the same adjacency data.
- *Editing.* A brush edits vertex radii within a geodesic radius of the hit point; editing finer
  than the current cut **force-subdivides and pins** that region (subdivided-because-edited is
  persistent; subdivided-because-near-camera is transient and may collapse).
- *Sampling (`PlanetSurface::heightAt(dir)`).* World point → containing triangle (barycentric on
  the sphere) → barycentric-blend the 3 vertex radii. One shared entry point used by building
  placement, picking, gravity, and Jolt collision-proxy generation.
- *Serialization.* Persist **only edited radius deltas** keyed by stable vertex id — never the
  mesh. The planet rebuilds its tree on load and re-applies deltas (rehydrate discipline, §2).
  Saves stay tiny.
- *Reuse / synergy:* `PlanetSurface` is the M0 helper §3.1 already calls for (point↔normal,
  `surfaceBasis`, cursor raycast) — `heightAt`/edit live there too. The horizon cull (§2.5-D) culls
  whole tree branches on the far hemisphere before traversal. Freeform building (§3.5) is
  unaffected: pieces still store `surface position + tangent heading` and just call `heightAt` for
  ground elevation — the terrain tree is **not** a build lattice, so "no global grid" still holds.
- *Risk:* the adjacency/crack handling across icosphere base-face seams is the genuine crux;
  prototype the sparse tree + `heightAt` standalone (CPU, glm-only) before wiring LOD into
  rendering.

**H. Orbital-mechanics simulation — the planet actually flies.**
The premise is a planet with **comically oversized rocket boosters** that **travels, changes orbit,
and aligns with other planets** (§3.1; the alignment windows that guarantee rare finds, §3.2). That
implies an **(approximate) Newtonian gravity model** — *not* a precise planet-body ephemeris, and
*not* a star-map fast-travel abstraction. It needs just enough real force that the player's planet
genuinely maneuvers and can pull off **gravity-assist slingshots under their own control**.
- *Bespoke, not Jolt.* A small custom integrator, **separate from Jolt** (which the plan keeps as a
  query engine only, §2.6, and explicitly *not* for dynamics). Runs on the CPU each tick into a
  `CelestialState`.
- *Fidelity — approximate, but it must *imply* gravity.* Per the design call, **not** an accurate
  planet-body simulation — just enough real force that **gravity assists / slingshots work**.
  - **Gravity sources are few and massive — the star + up to ~8 planets (≤9 wells total).** Only
    these exert gravity. Their *own* motion stays cheap/approximate (simple near-circular analytic
    orbits), so the system is **predictable and alignments are computable/schedulable** (§3.2).
  - **The player's planet is the one dynamic body:** each tick it sums inverse-square pull from the
    ≤9 wells + **booster thrust** and integrates (real Newtonian feel). Few wells = trivially cheap,
    and rich enough that **a well-timed pass around a planet is a real, controllable gravity assist**
    (save fuel / extend range) — the "implies the laws of gravity" the player can actually exploit.
  - **Everything smaller is non-gravitational decoration:** moons, spaceships, asteroid fields are
    **animated models on rails** (rotating/orbiting transforms) — purely cosmetic, neither feeling
    nor exerting gravity. Cheap visual life, zero sim cost.
- *Interaction — autopilot first, manual optional (no KSP required).* The default is full
  **autopilot**: the player picks a target (a body, an alignment window) and the system **plans and
  flies the whole maneuver** — **auto orbit-parking** (settle into a stable orbit around a body),
  **auto transfers**, and **auto orbit-swinging** (the autopilot itself sets up + executes
  gravity-assist slingshots to save fuel). Relocating the zoo is a **click-a-destination** action,
  never a piloting chore — **players never have to "play KSP" to move the planet** (this is the hard
  requirement, per the §1 north star). Manual thrust control is a **purely optional** layer for those
  who want to hand-fly a swing. *Implementation:* a small **maneuver planner** over the approximate
  model — numerically search burns that reach the target (and opportunistically route past a well for
  a free assist); it's part of this workstream, not an afterthought.
- *Ties:* **boosters** (§3.1) are the thrust source — destroyed at story start (§3.6), so the planet
  is **stuck in its current orbit** until rebuilt (the tutorial cage is literally orbital). **Local
  travel** (§3.1) = an in-region orbital transfer (Fuel); the **jump** tier stays a non-orbital warp
  (Jump Core). The moving wells feed the **skybox** (§2.5-F: nearby planets drift, the destination
  grows on approach); the decorative ships/asteroids are skybox/scene dressing too.
- *Open (§7):* mainly **how hands-on** the slingshot control is (light assist vs full manual burns);
  the fidelity model itself — ≤9 gravity wells, approximate, decorative small bodies — is now set.

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
- **Scale — inspired by Planetary Annihilation: Titans, but a slightly *smaller* planet.**
  - The whole planet is a small sphere with **pronounced curvature**: the player can zoom out to see
    the *entire* planet on screen and zoom in to surface level near a building. The far side is
    always hidden by the body (so horizon culling §2.5-D is very effective — the visible set is a
    real fraction of the world).
  - Structures/animals are **large relative to the surface** (PA-style), so only a compact zoo fits
    on one planet. This is a feature: surface area is a **soft cap**, and the game grows by
    **acquiring more planets / traveling** (§3.1 travel, §3.6 jumps), not by making one planet huge.
    It also keeps crowd/build counts — and thus the instanced-render budget (§2.5-A) — bounded.
  - **Comically large builds — towers can pierce the atmosphere.** Because everything is huge relative
    to the body, a tall structure is a *real fraction* of the planet's size — **the tallest towers
    literally poke up through the atmosphere shell (§2.5-E) into open space.** This is a signature
    visual (a chrome Atompunk spire glinting in vacuum above the glowing limb, §3.7) *and* a soft
    tradeoff: building **up** is a way to fight the surface soft-cap (vertical stacking, §3.5), but
    anything **above the atmosphere line is exposed to raw space** — radiation, no ambient
    life-support, full disaster exposure (§3.4) — so the top floors want the sealing/shielding the
    ground floors don't. *(How hard a mechanic vs mostly aesthetic: open §7.)*
  - **Camera:** a **PA-style free orbit** camera around the sphere (orbit + zoom from whole-planet to
    surface), with gravity toward the center. This resolves open question §7 toward free-orbit.
  - **Pick a concrete planet radius + piece base size in M0** and treat them as a tuning constant;
    everything surface-relative reads it from `PlanetSurface`.
- **Surface model:** **curved spherical planet from v1**, concretely a **geodesic-CDLOD icosphere
  whose subdivided triangle tree is the editable terrain** (§2.5-G). "The surface" is that tree;
  height is a per-vertex radius and is edited in place. The zoo is built on the surface of a
  small sphere ("tiny planet"). All placement, camera, and gravity are surface-relative:
  - Position on the surface is a point on the sphere; **"up" is the surface normal** (radial
    direction) at that point, and an object's orientation is built from that normal + a tangent
    heading. Centralize this in a `PlanetSurface` helper (`pointToNormal`, `surfaceBasis`,
    `project/raycast onto sphere`, `visibleFromCamera` horizon test — §2.5-D) so every system
    shares one convention.
  - Camera orbits/pans around the sphere; gravity points toward the planet center.
  - **Why this is the hard part:** snapping, footprints, path graphs, and enclosure flood-fill
    all have to work in spherical/tangent space rather than on a plane. Keep that math behind the
    `PlanetSurface` interface so gameplay code stays surface-agnostic. (Build/test it first in M0.)
- **Atmosphere:** each planet/system has its own sky and limb glow via the custom atmosphere
  shader pass (§2.5-E); scattering params are per star system and can shift with disasters.
- **Boosters:** visual + a `BoosterComponent` (thrust level, fuel burn rate). Gate **local travel**
  via fuel. **In story mode the booster starts *destroyed*** (the quasar strike, §3.6): with no
  booster the planet can't change orbit, so **all travel is locked during the tutorial** — and
  **rebuilding the booster is the tutorial's graduation gate** that unlocks local travel. (Inter-system
  jumps remain Jump-Core-gated beyond that.)
- **Orbital mechanics — the planet genuinely flies (engine workstream §2.5-H).** An **approximate
  Newtonian** model (not a precise sim): gravity comes only from the **star + up to ~8 planets**;
  moons, ships, and asteroid fields are **decorative animated props** (no gravity). The player's
  planet maneuvers under that gravity + **booster thrust** — change orbit, transfer toward bodies,
  **align** with them, and pull off **gravity-assist slingshots under player control**. Because the
  wells are predictable, **alignments are real, schedulable events** (star-map calendar) — the basis
  of the guaranteed-rare expeditions (§3.2). Interaction is **autopilot-first**: pick a destination or
  alignment and it auto-**parks** orbits, auto-**transfers**, and even auto-**swings** (gravity
  assists) for you — **no KSP required to move the zoo**; manual slingshots are optional flair (north
  star, §1).
- **Two travel tiers:**
  1. **Local travel (fuel):** the boosters fly the planet on an **orbital transfer** (§2.5-H) to
     nearby/connected systems or bodies within a reachable region. Routine, repeatable, gated by
     **Fuel** (a regular resource you refill).
  2. **Inter-system jump (special credit):** a long leap to a *new region* of the galaxy that the
     boosters can't reach. Consumes a scarce **Jump Core** (working name) — *not* purchasable with
     regular Credits and *not* refuelable. Jump Cores are **earned through progression**: campaign
     objectives and/or achievements (§3.6). This makes reaching new regions a milestone, not a grind.
- **Star map:** a separate UI/overlay (`OnDrawGui`) showing reachable star systems and, beyond
  them, **locked jump destinations**. Each entry lists: travel tier (fuel cost *or* Jump Core +
  unlock requirement), available alien species, civilization(s), disaster risk profile, and
  **upcoming orbital-alignment windows** (which rare species each guarantees, and when — §3.2),
  so the player can plan expeditions/travel around them.
- **Travel:** select destination → spend the right resource (Fuel for local, a Jump Core for a
  jump) → transit sequence (the skybox destination grows, §2.5-F) → arrive at a system that
  reshuffles available aliens, visitors, atmosphere, and the disaster table.
- **Systems vary — civilized vs barren:** some systems host an active **civilization** (a local
  tourist population, docking, reputation/sentiment to manage, §3.3); others are **barren** (no
  locals — visited for rare aliens, resources, or as a waypoint). A barren system earns **only** via
  the **inter-system** tourist stream, so a **`Warpgate`** (§3.3) is what makes parking there pay —
  without one, an empty system means no tourists and no income.
- **Resources:** `Fuel`, `Credits`, `Power`, `LifeSupport`, and `JumpCores` (the special,
  progression-only currency) — planet-wide sim values held in a singleton `PlanetState`.

### 3.2 Alien Collection
- **Species data** is *content*, authored in a data file (JSON/YAML) — not hardcoded:
  - model + animation set, biome (e.g. ice/lava/toxic/verdant/aquatic), temperature band, diet,
    social rule (solitary/herd), space needed, danger level, **rarity** (drives visitor appeal,
    §3.3), base visitor appeal, capture difficulty.
  - **Sentience (hidden flag):** some species are secretly **sentient**. It is *not* revealed at
    capture — the zoo's staff may classify a thinking being as a mere animal. Discovery and the
    ethical fallout of having caged it are a signature tourist/reputation mechanic (§3.3). The
    *discovery model* (hidden-and-revealed vs known) is open question §7.
- **Roster scope — ~50 species at launch, structured as a *matrix* (not a flat list).** An
  inter-system zoo should feel *abundant*: a target of **~50 species** (in line with classic Zoo
  Tycoon / Planet Zoo launch rosters), deliberately **more than one tiny planet can hold at once**
  (§3.1 soft cap) — so the collection itself *drives* travel and multi-planet growth rather than
  fitting on one world. To keep 50 from being reskins, spread them across orthogonal axes so each is
  a distinct **building puzzle**:
  - **Biome / medium:** terrestrial-easy → biome-specific (ice / lava / toxic / verdant) →
    **enclosure-only** exotics (aquatic → aquarium, gaseous → gas dome, §3.5). This axis also **paces
    the complexity onramp** (§3.6): early species need no special handling, later ones demand
    biospheres, biome walls, and sentient-grade care.
  - **Rarity:** common → rare → exotic/legendary (drives appeal §3.3 + capture difficulty).
  - **Social rule:** solitary / pair / herd (different space + count puzzles).
  - **Danger:** docile → dangerous (stronger barriers, §3.5).
  - **Sentience:** a **subset** (~1 in 6, not all) are secretly sentient (§3.3 ethics hook).
  - *Rough split:* **~10 starter/common terrestrial**, **~30 core biome-specific** across rarities,
    **~10 exotic/legendary** (enclosure-only, high-appeal, capture-hard, sentient-leaning). Launch may
    ship a slice and grow (Planet Zoo did), but ~50 is the "fills the fantasy" target.
- **Collection model — lifetime Zoopedia log, not simultaneous capacity.** Because one tiny planet
  can't hold all ~50 at once, "completing the collection" means having **successfully *housed*** each
  species at least once (logged permanently in the Zoopedia), *not* keeping all 50 on-planet at the
  same time. This decouples completionism from surface area, lets the player rotate/release and still
  progress, and keeps the soft-cap + travel loop intact. *(Open §7: lifetime-log vs multi-planet
  simultaneous ownership.)*
- **Discovery & capture — the explorer crew (a luck-based pull).** New species are found by sending
  an **explorer crew** on an expedition from the current system. The core is a **randomized pull**:
  spend resources (Credits/Fuel + crew time) and the crew returns a species drawn from the **current
  system's available pool** (§3.1), weighted by **rarity / capture difficulty** — mostly commons,
  occasionally a rare. It's a collection **slot-machine** (variable-ratio reward — a strong
  completionism hook, §9.1) that feeds the Zoopedia log and adds a new alien to display.
  - **Orbital-alignment windows guarantee rares.** Certain **rare/legendary** species *can't* be
    pulled reliably at random — they're tied to **celestial alignment events** ("when the planets
    align"), predictable astronomical windows surfaced on the star map / calendar (§3.1). Sending the
    crew **during** the right alignment **guarantees** that rare. This is the deliberate **anti-RNG
    layer**: pure luck for the *breadth* of the collection, but a **deterministic, plannable** path to
    the *specific* rares you want — watch the schedule, position the planet in the right system (§3.1
    travel), and pull the lever at the moment. Anticipation + planning = a "carrot with a clock" (§9).
  - **Duplicates aren't wasted.** Pulling an already-discovered species adds **another individual**
    (grows a herd / enables breeding) rather than being a dead result, and/or feeds a soft **pity**
    toward the next rare. (Exact duplicate handling: open §7.)
  - *Optional depth (v2):* the crew can be a small **upgradeable asset** (better crew/gear → better
    odds or access to deeper systems). v1 stays an abstract **roll + cost**; the crew-progression axis
    is open §7.
- **Habitat satisfaction:** a per-animal `Happiness` derived from how well its enclosure matches
  its species needs (biome props nearby, enough space, right temperature device, fed, social
  count). Happiness drives visitor appeal and breeding.
- **Welfare / mistreatment:** when `Happiness` stays low (wrong biome/temperature, hunger,
  overcrowding, sickness, disaster injury) the animal is **mistreated** — visible distress that
  *actively angers tourists who witness it* (§3.3), not merely a lower appeal score. Welfare is the
  bridge from "I built it" to "I must keep it well."
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
- **What draws tourists — the appeal model.** Tourist inflow and on-site spending scale with a
  computed **zoo appeal**, summed over exhibits and amenities:
  - *Per-exhibit appeal* = species **base appeal × rarity** (rare/exotic aliens pull hardest),
    scaled by the animals' **Happiness** (a thriving group beats a miserable lone specimen) and by
    **count / herd size** (a populated enclosure reads as a spectacle). **Large + happy + rare = big
    draw** — the three levers the player optimises.
  - *Supporting structures multiply appeal:* an exhibit framed by amenities, viewing platforms,
    decor, and themed props (§3.5) is worth more than a bare pen — supporting builds give a **local
    appeal bonus** to nearby exhibits, rewarding clustered, *designed* areas over scattered cages.
  - *Connectivity gates it:* an attraction earns **only if reachable on the path/road network**
    (§3.5 paths) — a disconnected exhibit draws no one. Roads are the circulatory system.
- **What repels tourists — welfare & ethics penalties.**
  - *Mistreatment:* witnessing a distressed/neglected animal (§3.2 welfare) applies **negative
    appeal** and bleeds reputation — one suffering exhibit can sour a whole area.
  - *The sentient-alien controversy — signature mechanic.* Some captured "animals" are actually
    **sentient** (§3.2), and the zoo's staff may have **mis-classified** them as non-sentient —
    caging a thinking being like livestock. While hidden it's a ticking liability; once **exposed**
    (observation over time, a research result, or a triggered event / a visitor noticing) it becomes
    a public scandal: tourists turn **angry**, reputation craters, and a **protest event** (runs on
    the §3.4 disaster cadence: warning → impact → aftermath) can trigger a mass tourist exodus.
    - *Player resolution — the dilemma:*
      1. **Properly house / uplift** it — a sentient-grade habitat (more space, enrichment, autonomy)
         or convert it from *exhibit* into a different draw (an **ambassador/"guest"** that pulls a
         *different, higher-value* tourist taste): trade raw exhibit appeal for reputation + a unique
         attraction.
      2. **Repatriate** it to its civilization → large reputation/reward with that faction (§3.3),
         but you lose the specimen.
      3. **Ignore it** → ongoing reputation drain and escalating protest risk.
    - This moral tension is what makes the space-zoo premise more than a reskin — it's a deliberate
      engagement hook (§9). Keep it *defensible*: the player must have had a path to discover and act
      (ties to §3.4's telegraph rule and §9.3's "challenge, not punishment").
- **Tourist sources — two streams.** Visitors arrive from two places:
  - **Local (host civilization):** while orbiting a system with a **civilization**, the **bulk of
    visitors are that host planet's citizens**, driven by that population's **sentiment** (below).
  - **Inter-system (off-world):** tourists travelling *from other systems* — a smaller, always-on
    baseline, **boosted by facilities like a `Warpgate`** (§3.5) that ease arrivals. Driven by your
    galaxy-wide **reputation** + warpgate throughput, **not** the local host's mood — so it keeps
    earning even when the local population sours. **Critical when parked in a system with no active
    civilization:** with no locals, off-world visitors are your *only* tourists, so a warpgate is the
    difference between a barren system earning nothing and staying viable (matters for resource/safety
    detours and long hauls, §3.1).
- **Source-population sentiment — the master stat for the *local* stream.** The host population's
  **sentiment** toward your zoo gates the dominant (local) tourist flow:
  - *Sentiment vs reputation:* **reputation** (above) is the slow, durable **faction standing** that
    gates unlocks/access; **sentiment** is the **live public mood** that directly drives **tourist
    inflow volume + spending willingness**. It's volatile — every welfare/controversy penalty above
    is at bottom a **hit to sentiment**, every great happy exhibit a boost. Sustained sentiment bleeds
    into reputation over time, and reputation sets a baseline sentiment.
  - *Per host system:* sentiment is tracked **per civilization / source planet**; travel (§3.1) swaps
    which population's sentiment is in play — a scandal in one system isn't automatic baggage in the
    next (though slower reputation can follow you).
  - *The core balance — sentiment vs profit.* Profit-maximising moves (mistreat for cheaper care,
    keep a sentient caged as a top exhibit) pay *now* but **erode sentiment**; let it crater and
    tourists dry up, protests fire (§3.4), the civilization may restrict access. The whole zoo economy
    is this tension (engagement §9.3) — and the advertisement lever below is how the player *cheats*
    it, at a price.
- **Advertisements — buying public opinion (the cynical lever).** A **comically large orbital
  advertisement board** (tonally matching the oversized boosters, §1) is an **orbital mega-structure**
  placed **in space, orbiting the planet** — a new *off-surface* placement domain (§3.5). It:
  - **Boosts tourist inflow** (raw awareness/draw), and
  - **Suppresses / nullifies negative sentiment** — marketing papering over the zoo's sins, so
    tourists keep coming despite welfare/controversy penalties.
  - *Cost & guardrail:* ads cost **Credits + Power**, and should not be a free "ignore ethics"
    button — model the suppression as **capped / diminishing**, and let an outright **scandal**
    (exposed sentient mistreatment) **overwhelm** it, so the player can lean on ads but not erase a
    real crisis. Whether suppression is **full or capped** is open question §7. This honours the
    satirical "just advertise harder" fantasy while keeping §9.3's *defensible, not free* line.
- **Misdeeds in private — the uninhabited-system discount (and its gamble).** With **no local
  population watching**, unethical acts (mistreatment, keeping a known sentient caged, culling,
  cutting welfare corners) cost **far less reputation** while parked in a **barren / civ-less system**
  (§3.1) — the zoo can do its dirty, profitable work where no one sees. **But word can still get
  out:** each misdeed carries a **leak chance** that fires the *full* scandal later (a delayed
  reputation hit + possible protest event, §3.4). Leak odds **rise with severity and with
  witnesses** — and the **`Warpgate`** is a double edge: it brings the off-world tourists that keep a
  barren system earning, but those same visitors are exactly who **carry the story home**. So the
  cynical play (jump somewhere empty, exploit quietly) is a real *gamble*, not a free pass — keeping
  §9.3's "defensible, not free" line. *(Discount size + leak odds: tuning, open §7.)*
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

**The quasar beam — apex catastrophe.** Distinct from the random risk-table events above, the
**quasar beam** is the campaign's scripted **inciting catastrophe** (§3.6): a near-total wipe that
opens the story by destroying the past-glory zoo and its booster. It is *not* on the routine risk
table (no realistic defense — it's a story reset, not a fight). A diminished, **survivable** version
may return as a rare **endgame super-threat** (telegraphed long, demanding the strongest defensive
builds) — the apex the disaster-defense systems ultimately exist for.

### 3.5 Building System (the centerpiece)
A detailed, creative editor that runs **in-game**, reusing engine pieces.

**Placement model — freeform precision (Planet Coaster style), NO global grid**
- **Why no grid:** a sphere cannot be tiled with uniform squares, so games that "grid-build" on a
  planet all pick a distorted compromise (cube-sphere → corner/seam warping; DSP-style adaptive
  latitude bands → lots of engine work; geodesic hexes → a tile idiom, not freeform). The freeform
  choice deliberately **sidesteps this** — there is no world lattice to distort. Confirmed direction.
- **Smooth free placement + rotation** on the sphere surface:
  - Objects sit on the surface oriented to the local normal; the player freely **slides** a piece
    across the surface and **rotates** it around the local "up" (surface normal).
  - **Snapping is relational, not to a world grid:** snap a piece to its *neighbors* — edge-to-edge,
    socket/connector points (fences chain, paths join, walls align), and optional surface-tangent
    angle increments. Hold a modifier to toggle snap off for fully free placement. (If players later
    want alignment aid, an optional cube-sphere "soft grid" snap target can be layered on — but it's
    not the underlying data model.)
  - Vertical stacking for multi-floor structures, with height offset along the surface normal.
- **Implication:** placement is a sphere raycast from the cursor → surface point → build a
  transform from `surfaceBasis(point)` + the player's heading angle. The gizmo and all snapping
  math operate in **tangent space** at the placement point (see `PlanetSurface`, §3.1). Pieces
  store a surface position + tangent heading, never a grid cell.
- **Build categories:**
  - *Terrain / ground (two layers, same geodesic tree §2.5-G):*
    1. **Elevation (heightmap):** a brush raises/lowers **per-vertex radius**, force-subdividing under
       the cursor for fine detail. Even, seamless everywhere (no pole pinch, no global grid).
    2. **Biome paint:** the player paints a **surface biome id** per-vertex (ice / lava / toxic /
       verdant / aquatic / …) alongside radius. Surface biome sets ground material, ambient props,
       and the *base* climate of **open-air** enclosures. Stored as per-vertex deltas like radius.
  - *Biomes & enclosed environments — biosphere / aquarium / gas dome:* beyond open-air biome paint,
    the player builds **sealed, climate-controlled environments** to simulate worlds the planet's
    ambient can't provide. A **biosphere** is a shell (dome / glass walls + roof) whose **interior
    climate is set by an interior machine** (heater/cooler/atmosphere/humidity — §3.5 *Functional*),
    *independent of the outside biome* (house a lava species under an ice sky). Its **medium** can be
    **air, water (aquarium), or gas**.
    - **Some biomes are enclosure-only** — they *cannot* be painted open-air and **require a sealed
      volume + a generator machine**. Example: a **gaseous** biome has **no walkable ground at all**;
      its whole environment is gas, so it only exists inside a gas dome with a gas-generator (aquatic
      likewise wants water — open "water biome" is shallow decor; real aquatic species want an
      aquarium). Open-air paint covers the *ground-based* biomes; machines cover the exotic ones.
    - The biosphere/machine is **user-scaleable** — the player sizes the volume; a bigger dome houses
      more/larger specimens at **higher power + maintenance cost** (the scaling is the spend/benefit
      dial).
    - Detected by **extending enclosure flood-fill to roofed/3D volumes** (see *Enclosure detection*
      below): walls **+** roof → a sealed `EnvironmentVolume` carrying its own temperature /
      atmosphere / **medium (air/water/gas)**; an open region inherits the painted surface biome +
      planet ambient. Sealing + powering it is the cost; an impossible habitat is the payoff. *(Open
      §7: biosphere as a discrete dome **prefab kit** vs purely **emergent** sealed-volume detection;
      default hybrid — prefab shell pieces whose enclosed volume is auto-detected.)*
  - *Biome walls & adjacency (Zootopia-inspired):* to keep two **different open-air biomes** sharply
    separated, the player runs a **biome wall** along their boundary — the giant climate-district
    walls of *Zootopia* (Tundratown beside Sahara Square). The wall holds the climate gradient, and
    its **upkeep (power/maintenance) scales with the *opposition* of the two biomes it divides ×
    boundary length**: **ice next to lava** is very expensive to hold; **verdant next to verdant** is
    ~free. This turns biome **layout into a spatial puzzle** — cluster compatible biomes, slot
    **buffer** biomes between extremes, or *pay* for the dramatic ice-beside-lava showpiece (a
    deliberate cost-vs-spectacle decision, §9). Biome walls are a special **Barrier** subtype (below);
    unwalled adjacent opposing biomes blend/degrade at the seam rather than holding a crisp edge.
  - *Paths / roads:* **dynamically generated** walkways/roads — the **circulatory network** tourists
    traverse and the visitor path graph reads (§3.3). Authored as control points on the surface; the
    mesh is built by `PathMeshBuilder` (§2.5-B) and supports **raise/lower** (height offset along the
    surface normal, with auto-generated supports/ramps for elevated sections). Regenerated on edit,
    not per frame. **An attraction earns tourists only while connected to this network** (§3.3
    appeal-gating) — laying roads to link exhibits is core, not decoration.
  - *Barriers:* fences/walls/glass (enclosure boundaries — define habitat regions), plus **biome
    walls** (the climate-separating subtype above, with opposition-scaled upkeep). Likely all
    dynamic-mesh-extruded along a path, sharing the path builder.
  - *Habitat props:* rocks, plants, water, biome-specific decor that feed happiness.
  - *Functional:* heaters/coolers, feeders, shield/radiation generators, power, life support.
  - *Attractions/visitor amenities:* food stalls, benches, viewing platforms (income).
  - *Decoration:* purely cosmetic.
  - *Orbital structures (off-surface — a second placement domain):* mega-structures placed **in
    space, orbiting the body** instead of on the surface — the **advertisement board** (§3.3: boosts
    inflow, suppresses negative sentiment) and the **`Warpgate`** (§3.3: a docking/arrival gate that
    raises the **inter-system tourist** stream — the lifeline in systems with no local civilization).
    Placement is a *different* gizmo mode: an **orbit** (radius + plane + phase) around the planet,
    not a surface raycast — positioned in space and animated along its orbit. Small but distinct from
    surface freeform building; the build UI gets a surface↔orbit mode toggle.
- **Tools:** place, delete, move, rotate, copy, multi-select, undo/redo, blueprint
  (save a selection as a reusable prefab — stored as a mini `.bmap`).
- **Validation:** placement rules (no overlap with existing geometry, paths need connection,
  enclosures need full barriers to count, functional devices need power). Overlap + "what's under
  the cursor" use **Jolt raycast/collide queries** (§2.6). Visual feedback (ghost preview,
  red = invalid).
- **Enclosure detection:** flood-fill / region system that turns a fully-barriered area into a
  named `Enclosure` with measured area, biome composition, temperature, and assigned species —
  the bridge between building and animal happiness.
  - **Open vs sealed:** the same flood-fill, extended to test for a **roof/dome**, distinguishes an
    **open-air enclosure** (inherits painted surface biome + planet ambient, §3.5 Terrain) from a
    **sealed `EnvironmentVolume`** (biosphere/aquarium: walls **+** roof → its own device-driven
    climate and **medium** air/water, decoupled from outside). Sealed volumes are what let the player
    keep an off-biome species anywhere — and are what radiation/cold disasters (§3.4) threaten, since
    a breach exposes the interior to planet ambient.

**Engine reuse**
- Reuse the **gizmo pattern** (`PoseGizmo`, `gizmo_render_system`) for a `BuildGizmo`.
- Every placed object is an **entity** with a `Placeable` component → the existing `.bmap`
  save/load *is* the zoo save system. Blueprints are sub-snapshots.
- Pieces are catalog entries (data file) → spawn a `ModelComponent` via the loader +
  optional `CollisionModelComponent` + functional component(s).

### 3.6 Progression: Campaign & Achievements (source of Jump Cores)

The special inter-system **Jump Core** currency (§3.1) is *earned*, not bought — so the game needs
a progression layer that grants it:
- **Campaign objectives:** a scripted spine of goals ("house 5 distinct biomes", "reach reputation
  X with the Vorn", "survive a meteor shower with zero animal losses"). Completing a campaign
  beat awards Jump Cores (and unlocks the next region's jump destination on the star map). This is
  the primary, authored progression path.
- **Achievements:** open-ended milestones (collection completeness, build/visitor/economy
  thresholds, disaster feats) that grant Jump Cores or other rewards. Secondary, replay-friendly.
- **Why gate jumps this way:** it turns "explore a new region" into a *reward for mastering the
  current one*, pacing content and preventing players from skipping ahead with raw money.
- **Implementation:** an `ObjectiveSystem` evaluating data-driven objective/achievement definitions
  (predicates over `PlanetState`, registry views, and event hooks) against game state each tick,
  emitting completion events that credit `PlanetState.jumpCores` and flip star-map unlock flags.
  Definitions live in `assets/campaign/*.json` + `assets/achievements/*.json`. Progress is
  serialized with the save (new `ProgressionState` in the snapshot).

**Progression *shape* — onramp + aspiration hook**

- **Complexity onramp — start as a *regular* zoo in space.** The opening hours deliberately use
  **easy species** that need **no special biome, no machine, no ethical handling** — common critters
  behind simple fences on open-air ground. It's a normal zoo that just happens to be in space: the
  player learns the core **build → economy → guests** loop with *none* of the exotic systems on.
  Complexity then unlocks **in waves**, one system per campaign beat/region so the player is never
  flooded: **special biomes + biome walls (§3.5) → biospheres / gas domes → disasters (§3.4) →
  sentient species + the sentiment/ethics economy (§3.3) → travel & jumps (§3.1)**. (This unlock
  order also lets the in-game ramp track engine availability — the M-milestones §6 light these
  systems up in roughly the same sequence.)
- **Story premise — the quasar strike (and the endgame-glimpse hook it earns).** The campaign
  **opens on the zoo at its past glory** — exotic gas-dome and lava habitats, a **sentient ambassador**
  drawing crowds, a warpgate feeding tourists, the planet wreathed in atmosphere — and then a **quasar
  beam sweeps the system and wipes it out**: structures gone, animals lost, the **booster destroyed**,
  leaving a bare planet adrift. The campaign goal is to **rebuild that past glory** (and surpass it).
  This makes the aspiration glimpse **diegetic** — not a vision board but *memory*: the player has
  literally seen, and briefly operated, the very zoo they're rebuilding toward. (Implementation: the
  glory zoo is a hand-authored `.bmap` toured/operated on rails in the cold open, then the quasar
  "resets" to a near-empty starting save — cheap, since both are just saves.) Reinforced afterward by
  the **star map's locked wonders** (§3.1) and locked Zoopedia entries (§9.3 "always a carrot"). For a
  slow-burn tycoon economy, **loss-then-restoration** is the single strongest opening hook: *show the
  dream, take it away, make them earn it back.*
- **Tutorial cage — no booster, no travel.** Because the quasar destroyed the **booster**, the player
  **cannot change orbit / travel during the tutorial** — stranded in the opening system. This is the
  natural onboarding constraint: learn the core **build → economy → guests** loop in one fixed place
  with the exotic systems still locked (the onramp above). **Rebuilding the booster is the tutorial's
  graduation** — the act that *unlocks local travel* (§3.1) and opens the galaxy plus the rest of the
  complexity ramp. (Inter-system **jumps** stay gated behind **Jump Cores**, §3.1, even after the
  booster is restored — so travel unlocks in two tiers, matching §3.1's two travel tiers.)

> **Open scope question (§7):** is jump-currency tied to the **campaign**, **achievements**, or
> **both**? Default assumption: campaign is the main faucet, achievements a bonus faucet.

### 3.7 Art direction & visual identity

**Cartoony Atompunk, reimagined with PBR.** The whole game leans **cartoonish** — exaggerated,
charming, readable — matching its tongue-in-cheek premise (comically oversized boosters §1, "just
advertise harder" §3.3, the satirical sentient-alien ethics §3.3). It is deliberately **not**
photoreal. The flavour is **Atompunk / Raygun Gothic** (1950s–60s optimistic futurism): chrome and
fins, Googie curves, ray-gun silhouettes, glass domes; a bright pastel-and-chrome palette with
neon/emissive accents.

**"Reimagined with PBR" = stylized PBR, not realism.** The engine's deferred **PBR** pipeline +
**atmosphere scattering (§2.5-E)** + **bloom** carry the *light response* — real
metallic/roughness/emissive/glass — while forms and palette stay cartoony: saturated albedos, clean
surfaces, punchy emissive, strong rim/atmosphere glow. PBR is what makes even a low-detail chrome
rocket or a glowing reactor read as "expensive"; keep the **materials** rich and the **geometry**
stylized.

**Poly budget — the *Cities: Skylines* split.** Detail goes where the player looks; cheapness goes
where the engine is stressed — and this split *is* the engine's two render paths, not just taste:
- **Static structures (buildings, habitats, domes, props, terrain) — relatively HIGH-poly /
  detailed.** They're what the player builds, frames, and admires; their count is bounded and they
  ride the standard high-detail **G-buffer** path (`GBufferRenderSystem`). Spend the geometry here.
- **Crowds (tourists, aliens, agents) — LOW-poly.** Many, instanced, animated — kept cheap: simple
  charming silhouettes that read in a herd and animate cleanly. They ride the **instanced skinned
  path (§2.5-A)** with aggressive LOD/impostors + the horizon cull (§2.5-D).
- So the aesthetic decision and the rendering architecture reinforce each other: lavish built
  environment, cheap animated agents — protecting the headline crowd feature while the zoo still
  looks rich.

**Why this serves the game:** a cohesive, **screenshot-able, ownable** identity (§9.8) that plays to
the engine's strengths, keeps catalog-heavy content production **survivable** (§8 content risk), and
matches the comedic tone — instead of an unwinnable photoreal race with AAA zoo sims. *(The asset
**pipeline** question — placeholder primitives vs sourcing real assets early — is separate and stays
open, §7-#7; this decides the *target look*, not the production order.)*

---

## 4. New ECS components (game layer)

All serialized ones follow the rehydrate discipline (§2). Names provisional.

- `PlaceableComponent` — catalog id, category, **surface position + tangent heading** (no grid
  cell), footprint, connector/socket points for relational snapping, integrity/health.
- `EnclosureComponent` — region cells, **open vs sealed**, **medium (air/water/gas)**, biome mix,
  temperature/atmosphere (machine-driven when sealed), **scale** (user-sized biosphere → power/upkeep),
  area, assigned species, contained animals. Sealed = the biosphere/aquarium/gas-dome
  `EnvironmentVolume` (§3.5); open = inherits surface biome + ambient. Some biomes (e.g. gaseous) are
  **sealed-only**.
- `BiomeWallComponent` — a climate-separating barrier (§3.5): the two biomes it divides + boundary
  length → **opposition-scaled power/maintenance upkeep** (ice|lava = costly, like|like ≈ free).
- `AlienComponent` — species id, happiness, hunger, health, social state, home enclosure entity,
  **welfare/mistreatment state** (§3.2), and **sentience runtime** (is-sentient + *discovered?* flag,
  + chosen resolution: exhibit / ambassador / repatriated — §3.3).
- `SpeciesDef` (data, not component) — needs/appeal/model, **rarity**, **sentient** flag (hidden);
  loaded from `assets/species/*.json`.
- `VisitorComponent` — credits, satisfaction, target habitat, path progress, lifetime, **source
  civilization** (which host population they came from, §3.3).
- `BuildingFunctionComponent` — type (heater/cooler/feeder/shield/power/lifesupport), power draw, radius, active.
- `AdvertisementComponent` — orbital mega-structure (§3.3): **orbit** (radius/plane/phase),
  tourist-draw boost, **negative-sentiment suppression** amount, power draw. Placed off-surface (§3.5).
- `WarpgateComponent` — orbital mega-structure (§3.3): **inter-system tourist throughput** boost
  (the off-world visitor stream; the *only* tourism source in civ-less systems), power draw, orbit.
- `BoosterComponent` — thrust, fuel rate (planet-level, few instances). The **actuator** for the
  orbital sim (§2.5-H): thrust feeds the player planet's Newtonian integration.
- `CelestialState` (singleton/context, serialized) — the orbital sim (§2.5-H): the **≤9 gravity
  wells** (star + ≤8 planets) on approximate analytic orbits, the **player planet's live
  position/velocity** + the active **autopilot maneuver plan** (parking / transfer / swing burns), and
  the **computed alignment windows** (star map + §3.2). Decorative small bodies (moons / ships /
  asteroid fields) are animated props, *not* part of this state.
- `PlanetState` (singleton/context) — credits, **fuel**, **jumpCores** (special travel currency),
  power, life-support, current system, date.
- `ProgressionState` (singleton/context, serialized) — completed campaign beats, unlocked
  achievements, star-map jump unlocks; the ledger the `ObjectiveSystem` reads/writes (§3.6).
- `DisasterState` (singleton/context) — active events, timers, risk table for current system.
- `CivilizationState` (per source faction, serialized) — **reputation** (durable standing, gates
  unlocks) + **sentiment** (live tourist mood that drives inflow, §3.3) + taste profile + ticket
  tolerance + unlock flags. The active host's sentiment (minus ad suppression) sets current tourism.
- `PathNodeComponent` / path graph — connectivity for visitor navigation.

**Rendering / geometry layer (§2.5):**
- `InstancedSkinnedComponent` — per-instance transform + animation phase/clip for a crowd of one
  skinned model; backs the instanced skinned draw (§2.5-A). Per-instance SSBO is transient.
- `DynamicMeshComponent` — runtime-generated geometry (control points + params → verts/indices);
  used by paths and barriers (§2.5-B). Persists the *recipe* (control points/heights), rebuilds
  buffers on rehydrate.
- `ParticleSystemComponent` — emitter params + live particle pool for space effects (§2.5-C);
  emitter config serialized, live particles transient.
- `PlanetTerrainComponent` — the geodesic-CDLOD planet surface (§2.5-G): planet radius/seed +
  the sparse triangle tree and pooled vertices (transient, rebuilt on load) and the **edited
  radius/biome deltas** keyed by stable vertex id (the *only* serialized part). Backs
  `PlanetSurface::heightAt`/edit. One per planet (few instances).

---

## 5. Architecture & file plan

```
src/render_systems/                # ENGINE-level additions (§2.5), not game-specific
  planet_terrain_render_system.*              # G: draws the LOD cut + CDLOD morph in the vertex shader
  instanced_skinned_gbuffer_render_system.*  # A: instanced + skinned deferred pass
  particle_render_system.*                    # C: instanced billboard particles
  atmosphere_render_system.*                  # E: full-screen atmospheric scattering pass
  skybox_render_system.*                      # F: full-screen procedural deep-space backdrop
shaders/
  atmosphere.frag                  # E: Rayleigh/Mie scattering (+ .vert reuses full-screen tri)
  skybox.frag                      # F: procedural stars/galaxies/nearby planets from view ray
src/animation/ or src/geometry/
  path_mesh_builder.*              # B: control-points -> surface ribbon mesh (raise/lower)
src/game/
  space_zoo_application.hpp/.cpp   # SpaceZooApplication : Application (the MyApplication analog)
  planet_terrain.*                 # G: sparse geodesic-CDLOD triangle tree, per-vertex radius,
                                   #    lazy subdivide/collapse, LOD cut, heightAt, brush edits
  planet_surface.*                 # sphere math: point<->normal, surfaceBasis, cursor raycast
                                   #    (thin facade over planet_terrain for height/edit queries)
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
    travel_system.*                # star map, fuel (local) + Jump Core (jump) transitions
    objective_system.*             # campaign/achievement eval -> grants Jump Cores (§3.6)
  data/
    species_db.*  catalog_db.*  civilization_db.*  star_map.*
    campaign_db.*  achievement_db.*                # objective/achievement definitions
  ui/
    star_map_panel.*  build_panel.*  hud_panel.*  objectives_panel.*  # OnDrawGui ImGui panels
  game_components.hpp              # the new ECS components above
assets/
  species/*.json  catalog/*.json  systems/*.json   # authored content
  campaign/*.json  achievements/*.json             # progression definitions (§3.6)
docs/space_zoo_plan.md            # this file
maps/                             # save games + blueprints (.bmap), already gitignored-ish
```

**Console commands** (Source-style, added to `ConsoleCommand`): `give_credits`, `give_fuel`,
`give_jumpcore`, `spawn_alien <species>`, `travel <system>`, `jump <system>`,
`complete_objective <id>` (debug-grant progression), `trigger <disaster>`, `buildmode <0|1>`,
`save_zoo <name>` / `load_zoo <name>` (wrap `Map::save/load` + rehydrate).

**Update flow** in `SpaceZooApplication::OnUpdate(dt)`: economy tick → disaster director →
alien sim → visitor sim → objective/achievement eval → build-mode input. `OnDrawGui`: HUD
(credits/fuel/Jump Cores) + active panel (star map / build / objectives).

---

## 6. Milestones (incremental, each independently runnable)

**M0 — Skeleton + geodesic planet surface (1st PR)**
- `SpaceZooApplication` subclass wired into the build; the **geodesic-CDLOD planet (§2.5-G)** as the
  surface — sparse icosphere triangle tree, distance-based LOD cut, CDLOD morph, and per-vertex
  radius height (a simple raise/lower brush proves edit-anywhere) — plus a placeholder starfield
  background (the full procedural skybox/atmosphere lands in E4, §2.5-E/F), a surface-orbiting camera
  and center-pointing gravity. Implement and unit-feel-test the `PlanetSurface` helper (point↔normal,
  `surfaceBasis`, cursor→sphere raycast, `heightAt`) over the terrain tree — everything else depends
  on it. *Prototype the sparse tree + `heightAt` standalone (CPU/glm) first, then wire LOD into
  rendering.* HUD shows placeholder resources. Loads/saves an empty zoo `.bmap` (edited radius
  deltas only).

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

**E4 — Atmosphere + skybox shaders (§2.5-E, §2.5-F)** *(visual identity; pairs with M5)*
- `SkyboxRenderSystem` + `skybox.frag` procedural deep-space backdrop (stars/galaxies/nearby
  planets), then `AtmosphereRenderSystem` + `atmosphere.frag` scattering over it — both before
  bloom (skybox → atmosphere → bloom → composite). Per-star-system params. A flat starfield can
  land first; galaxies, nearby bodies, and the approaching destination planet follow.

**M5 — Travel (local tier)**
- Star map UI, **fuel-gated local** system travel, per-system species/civilization/disaster
  reshuffle. Per-system atmosphere params (E4) sell the change of sky on arrival. Jump
  destinations show as **locked** here (unlocked in M8).

**E3 — Particle system (§2.5-C)** *(prereq for M6 spectacle)*
- `ParticleSystemComponent` + `ParticleRenderSystem` (instanced billboards, additive, bloom-aware):
  meteor streaks, radiation shimmer, booster exhaust.

**M6 — Disasters** *(uses E3)*
- Disaster director with meteor + radiation events, particle spectacle, warning UI, defensive
  buildings that mitigate.

**M8 — Progression & inter-system jumps (§3.6)**
- `ObjectiveSystem` + data-driven campaign/achievement definitions; objectives award **Jump Cores**
  and flip star-map jump unlocks; the **jump** travel tier spends a Jump Core to reach a new region.
  `ProgressionState` serialized with the save. (Depends on M5 + a few systems to write objectives
  against, hence late; an early debug `give_jumpcore`/`jump` exists from M5 for testing.)

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
4. **Building snap model** — ✅ **Freeform + relational snapping, NO global grid** (§3.5). A sphere
   can't hold a uniform square grid; pieces store surface position + tangent heading and snap to
   neighbors. Optional cube-sphere "soft grid" snap target may be layered on later, not as the
   data model.
5. **Scale & camera** — ✅ **PA: Titans-inspired, slightly smaller planet** (§3.1): strong curvature,
   whole planet viewable, growth via more planets not bigger ones, and a **free-orbit camera**.
6. **Planet topology & terrain** — ✅ **Geodesic-CDLOD icosphere; the subdivided triangle tree IS
   the editable terrain** (§2.5-G, decided 2026-06-21). Sparse/lazy subdivision near camera + under
   edits, per-vertex radius for height, edit-anywhere with no pole pinch / no global grid. **Seam
   handling: subdivided edge verts snap to the lower-LOD neighbour's edge** (T-junction removal by
   snapping) rather than a full distance morph — see §2.5-G.
14. **Art direction** — ✅ **Cartoony Atompunk / Raygun Gothic, reimagined with stylized PBR**
    (§3.7, decided 2026-06-22). Not photoreal: bright pastel-and-chrome palette, neon/emissive
    accents, rich PBR materials + atmosphere/bloom over **cartoony forms**. Poly budget split
    *Cities: Skylines*-style — **high-poly detailed static structures** (G-buffer path) vs **low-poly
    instanced crowds** (§2.5-A). The asset *pipeline* timing stays open (§7-#7).

**Still open (confirm before/at M1):**
7. **Art pipeline** — author placeholder primitives/generated models first, or source GLTF assets early?
8. **Scope of "planet moves"** — ✅ *largely resolved* toward **full simulated flight**: the planet
   moves via a real Newtonian orbital sim (§2.5-H, decided direction 2026-06-22), not a fast-travel
   abstraction. Remaining nuance (fidelity + interaction depth) lives in §7-#18.
9. **Jump-currency faucet (§3.6)** — are Jump Cores earned via the **campaign**, **achievements**,
   or **both**? Working default: campaign is the primary faucet, achievements a secondary bonus.
   (Also: is there a *fully sandbox* mode where jumps are unlocked/free?)
10. **Biosphere / aquarium model (§3.5)** — discrete dome **prefab kit**, purely **emergent**
    sealed-volume detection, or **hybrid**? Working default: hybrid — prefab shell pieces whose
    enclosed roofed volume is auto-detected as an `EnvironmentVolume`.
11. **Sentience discovery model (§3.2/§3.3)** — is a species' sentience **hidden and discovered**
    (observation/research/event → ethical surprise) or **known at capture**? And what does "properly
    housing" a sentient yield — keep as a higher-value **ambassador** attraction, or only
    **repatriation** for reputation? Working default: hidden-and-discovered; both ambassador and
    repatriation are available player resolutions (§3.3).
12. **Advertisement sentiment-suppression (§3.3)** — do orbital ads **fully nullify** negative
    sentiment (pure "advertise harder" power fantasy) or **cap/diminish** it so a real scandal still
    breaks through? Working default: **capped + diminishing, scandal-overwhelmable** (keeps the
    sentiment-vs-profit balance and §9.3's "defensible, not free"). Confirm before M4/M6.
13. **Uninhabited-system misdeed discount + leak (§3.3)** — how big is the reputation discount for
    unethical acts in a barren system, and how punishing are the **leak odds** (and do they scale with
    severity / warpgate-tourist traffic)? It must stay a *gamble*, not a safe-haven loophole. Tuning,
    confirm around M6/M8.
15. **Collection model (§3.2)** — is "collect them all" a **lifetime Zoopedia log** (housed each of
    the ~50 species at least once) or **simultaneous** (requires multi-planet ownership to hold all at
    once)? Working default: **lifetime log**, decoupling completionism from one planet's soft cap.
16. **Above-atmosphere building (§3.1)** — is a tower poking past the atmosphere a **real mechanic**
    (its exposed floors take space hazards / need sealing) or **mostly aesthetic**? Working default:
    a *soft* mechanic — above-line = exposed (radiation / no ambient life-support / disaster), so
    height is a real tradeoff, not just a look.
17. **Expedition depth (§3.2)** — how are **duplicate** pulls handled (extra individual for
    herds/breeding, soft **pity** toward rares, or refund)? And is the **explorer crew** an
    upgradeable asset (better odds/range) or a flat roll + cost? Working default: duplicates →
    extra individual + light pity; crew upgrades are a v2 depth axis.
18. **Orbital mechanics (§2.5-H/§3.1)** — ✅ *resolved.* Fidelity: approximate Newtonian; gravity
    only from the **star + ≤8 planets** (≤9 wells); player planet is the one dynamic body; moons /
    ships / asteroids are decorative props. Interaction: **autopilot-first** — the system auto-parks
    orbits, auto-transfers, and auto-executes gravity-assist swings, so **players never need
    KSP-style piloting to move the zoo**; manual slingshots are optional flair only. (Supersedes old
    §7-#8 toward full simulated flight.)

---

## 8. Risks
- **Engine rendering/geometry work is the dominant risk** (§2.5), ahead of the gameplay sim. By
  difficulty: (A) instanced *skinned* rendering is net-new and the headline feature — prototype it
  standalone (E2) before crowds depend on it; (B) editable dynamic path meshes need a safe
  buffer-rebuild path respecting move-only buffer ownership; (C) particle system; (E) atmosphere
  and (F) skybox shaders are lower-risk (they reuse the existing full-screen-pass plumbing) but are
  the game's visual identity. A–C share the per-instance-SSBO backbone, E/F share the full-screen
  pass — build each backbone once.
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
  algorithms and revisit only if profiling (`showProfile`) flags it. The tiny-planet **horizon cull
  (§2.5-D)** is the cheapest big lever for crowd render cost — apply it during per-instance SSBO
  build so off-side agents cost nothing to draw.

---

## 9. Engagement & retention — why each loop is fun

The sections above list *systems*; this one states the **hook each system must deliver** — the
psychological reason the genre (RollerCoaster Tycoon, Planet Coaster, Zoo Tycoon, Planet Zoo) is
addictive. It exists so feature work stays honest: a system that's present but doesn't serve a hook
below is a chore. **Litmus test for every mechanic: is this a *decision* or a *chore*?** Chores kill
retention; push them to automation/staff and keep the player on the creative/decision layer. And
*above* this sits §1's **design north star**: even a fun system is cut or simplified if it doesn't
make the **collection, building, or profit** loop better — supporting systems serve the core, never
compete with it.

### 9.1 The engagement pillars (the retention hooks)

1. **The building tool *is* the toy — "just one more piece."** The genre's #1 hook: placing and
   tweaking is intrinsically satisfying *before any reward*. People play sandbox with money off for
   hours. This lives or dies on **feel** — responsive gizmo, snapping that feels good, ghost preview,
   instant undo. Our freeform-precision building (§3.5) is the right bet, but ~70% of the addiction
   battle is won or lost on whether placement *feels* good. Treat build-feel as a first-class,
   polish-early concern, not a thing to tune later.
2. **A tight early economy that snowballs.** The dopamine is *compounding growth*: money → build →
   guests → more money → bigger build. The craft is the **early scarcity curve** — the first ~20 min
   must make every Credit a real decision (this habitat *or* the heater?), then it snowballs and the
   player feels powerful. Starting rich is boring; staying tight is stressful. Our extra lever: keep
   **Credits** snowballing but gate *travel* behind scarcer **Fuel**/**Jump Cores** (§3.1) so growth
   never fully trivializes.
3. **Always a carrot — layered goals (30 s / 30 min / 3 h).** An immediate goal ("this habitat needs
   a heater"), a session goal ("hit reputation X"), and a long arc ("reach the next region"). Our
   **campaign beats → Jump Cores → new region** chain (§3.6) is exactly this spine. The unlock drip
   (species, parts, regions) is the "numbers go up" of content. **Always show the locked next thing
   and its requirement** — an invisible carrot stops pulling.
4. **A living world you *observe*, not just manage — the "ant farm".** Half the playtime is *watching*
   little guests walk the paths, react, complain ("I want to see the lava beast"), and animals do
   species-specific things. Our instanced skinned crowds + wandering aliens (§2.5-A, §3.2) are the
   tech; the magic is **legible personality** — guest thoughts, visible cause→effect, animals
   reacting to biome/each other. This is what players screenshot and share.
5. **Legible, immediate feedback — meters and heatmaps.** Every action gets an instant, visible
   response: happiness, zoo rating, guest-density heatmaps, "this enclosure is too cold." The hook is
   watching a meter move *because of a thing you did*. Keep happiness/appeal **transparent**
   (checklist-style, §3.2/§3.5) — never a black box; the player must see the lever *and* the result.
6. **Collection & completionism.** "Gotta house every species." A Zoopedia/codex with locked
   silhouettes is a quiet but strong driver. Our data-driven alien collection (§3.2) is built for it;
   the real puzzle is *housing* a species well (meeting its needs), not merely owning it.
7. **Manageable crises — failure *with agency*.** Pure builders go sleepy; tension you can *design
   against* keeps engagement. Our **disasters** (§3.4) are this hook and are better than most because
   they reward *defensive building* (shields, bunkers). Rules: **always telegraph** (warning → impact
   → aftermath, already in §3.4), make damage **recoverable**, and make the defense a **building
   decision**. A loss the player couldn't prevent feels unfair; one they *should have* prepared for is
   addictive.
8. **Ownership, identity, sharing.** "This is *my* zoo" → screenshots → blueprint sharing → social
   loop. Our blueprint system (mini-`.bmap`, §3.5) is the seed. The tiny-planet + atmosphere-rim look
   (§2.5-E/F) is inherently shareable in a way a flat park isn't — the planet looks like a *screenshot*
   by default; lean into that as a marketing/retention asset, not just aesthetics.

### 9.2 The meta-lesson

**Keep the player on the creative/decision layer; automate the tedium.** The genre's failure mode is
micromanagement grind (manually feeding animals, chasing one angry guest). The beloved games push the
boring loops to automation/staff and reserve the human for *interesting* choices: what to build, where
to travel, how to defend. Re-apply the §9 litmus test to every new system.

### 9.3 Our structural advantage — and its two specific risks

**Advantage: the zoo *is* the vehicle (§3.1).** Travel normally fragments tycoon investment (build a
park, then… a new park?). Because our planet *moves with the player*, travel adds novelty (new
species, skies, disasters) **without discarding the build** — every system reinforces one persistent
creation. That's a genuine retention edge, not just a theme.

Two risks unique to this twist, to hold the line on:
- **Travel must never feel like it resets progress.** Arriving somewhere new must *add* (new
  collectibles/threats/sky), never invalidate the zoo already built. If players feel travel costs them
  their work, they stop travelling — which kills the §3.1/§3.6 progression spine.
- **Disasters must stay "defensible challenge," not "random punishment."** The instant a loss feels
  unavoidable, tension flips from addictive to resentful. Every disaster needs a *prevent/mitigate*
  building answer that was available *before* it struck (ties directly to §3.4's telegraph phases).
