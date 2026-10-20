#include "imgui/components/component_panels.hpp"

#include "ecs/bagel_ecs_components.hpp"
#include "imgui.h"

#include <random>

namespace bagel
{
namespace
{
// Upper bound on a layer's elevation scale. Beyond this the displacement swamps the radius and
// the planet turns into spikes, so the slider clamps here (typed entry included).
constexpr float kMaxNoiseStrength = 1.5f;

// Shared cap for both roughness sliders: the starting frequency (baseRoughness) and the
// per-octave frequency multiplier (roughness / lacunarity). Past ~2 the octaves outrun the mesh
// resolution and the terrain just aliases into noise.
constexpr float kMaxRoughness = 2.0f;

// Re-roll one noise layer into a different, still plausible-looking terrain.
//
// `center` is the offset into the noise field (both filters sample `point * frequency + center`),
// so a random one is effectively a new seed — that alone gives completely different terrain. The
// shape parameters are re-rolled too, but each within a deliberately narrower band than its
// slider allows: the extremes of those sliders produce mush or spikes, and a randomize button is
// only useful if most clicks land on something worth looking at. `type`, `enabled` and the mask
// flag are left alone — those are the author's choices, not part of "the noise".
void randomizeNoiseLayer(NoiseSettings &n)
{
    static std::mt19937 rng{std::random_device{}()};
    auto uniform = [](float lo, float hi) {
        return std::uniform_real_distribution<float>{lo, hi}(rng);
    };

    n.center = {uniform(-100.0f, 100.0f), uniform(-100.0f, 100.0f), uniform(-100.0f, 100.0f)};
    n.numLayers = std::uniform_int_distribution<int>{3, 8}(rng); // fBm octaves
    n.baseRoughness = uniform(0.5f, kMaxRoughness);
    n.roughness = uniform(1.5f, kMaxRoughness); // lacunarity; 2 is the classic octave doubling
    n.persistence = uniform(0.35f, 0.65f);  // near 1 accumulates into noise-on-noise
    n.minValue = uniform(0.7f, 1.2f);       // floor that carves out the flat/ocean regions
    n.strength = uniform(0.05f, 0.5f);      // well under the cap: random 1.5 is never usable
}
} // namespace

void DrawPlanetComponent(entt::registry &registry, entt::entity entity)
{
    auto *pc = registry.try_get<PlanetComponent>(entity);
    if (!pc)
        return;

    ImGui::SeparatorText("Planet");
    // Any edit flags the component dirty; the per-frame planet pass in OnUpdate
    // regenerates the mesh and clears the flag (live update).
    bool changed = false;
    changed |= ImGui::DragFloat("Radius", &pc->radius, 0.5f, 0.1f, 100000.0f);

    // 1 = sphere, 0 = the raw cube the mesh is generated from (each vertex's distance from the
    // origin is lerped between the two). Terrain noise is unaffected — only the base shape morphs.
    changed |= ImGui::SliderFloat("Sphereness", &pc->sphereness, 0.0f, 1.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Cube"))
    {
        pc->sphereness = 0.0f;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Sphere"))
    {
        pc->sphereness = 1.0f;
        changed = true;
    }

    int res = static_cast<int>(pc->resolution);
    if (ImGui::DragInt("Resolution", &res, 0.25f, 2, MAX_PLANET_RESOLUTION))
    {
        pc->resolution = static_cast<uint32_t>(res < 2 ? 2 : res); // resolution-1 is used as a divisor
        changed = true;
    }

    // Fixed array of noise layers; elevation is the sum of every enabled one. Disabled layers stay
    // in the array so their settings are preserved when toggled back on.
    ImGui::TextUnformatted("Noise layers");
    for (int i = 0; i < MAX_NOISE_LAYERS; ++i)
    {
        ImGui::PushID(i);
        NoiseLayer &layer = pc->layers[i];
        changed |= ImGui::Checkbox("##enabled", &layer.enabled); // per-layer on/off
        ImGui::SameLine();
        if (ImGui::TreeNode("layer", "Layer %d%s", i, layer.enabled ? "" : " (off)"))
        {
            NoiseSettings &n = layer.settings;
            // Per-layer re-roll: new seed (center) + new shape, leaving the other layers alone.
            if (ImGui::Button("Randomize"))
            {
                randomizeNoiseLayer(n);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(new seed + shape)");
            // Keep order in sync with the NoiseType enum (SIMPLE, RIDGES).
            static const char *noiseTypeNames[] = {"Simple", "Ridges"};
            int type = static_cast<int>(layer.type);
            if (ImGui::Combo("Type", &type, noiseTypeNames, IM_ARRAYSIZE(noiseTypeNames)))
            {
                layer.type = static_cast<NoiseType>(type);
                changed = true;
            }
            changed |= ImGui::Checkbox("Masked", &layer.useFirstLayerAsMask);
            changed |= ImGui::DragInt("Octaves", &n.numLayers, 0.1f, 1, 8); // fBm octaves within this layer
            // AlwaysClamp so ctrl+click typed entry respects the cap too — a plain DragFloat
            // only clamps while dragging.
            changed |= ImGui::DragFloat("Strength", &n.strength, 0.01f, 0.0f, kMaxNoiseStrength,
                                        "%.3f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Base roughness", &n.baseRoughness, 0.01f, 0.0f, kMaxRoughness,
                                        "%.3f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Roughness (lacunarity)", &n.roughness, 0.01f, 0.0f, kMaxRoughness,
                                        "%.3f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Persistence", &n.persistence, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Min value", &n.minValue, 0.01f, 0.0f, 10.0f);
            changed |= ImGui::DragFloat3("Center", &n.center.x, 0.01f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (changed)
        pc->dirty = true; // geometry change → rebuild the mesh

    // Elevation colour gradient (planets have no texture maps). height is a normalized [0,1] key:
    // 0 = the mesh's lowest point, 1 = its highest. Keep stops sorted by height. These do NOT set
    // dirty: the render system reads the gradient from the component every frame and pushes it, so
    // gradient tweaks are live without regenerating the mesh.
    ImGui::SeparatorText("Colour gradient");
    for (int i = 0; i < pc->gradientCount; ++i)
    {
        ImGui::PushID(1000 + i);
        GradientPoint &g = pc->gradient[i];
        ImGui::ColorEdit3("##color", &g.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::SliderFloat("Height", &g.height, 0.0f, 1.0f);
        ImGui::PopID();
    }
    if (pc->gradientCount < MAX_GRADIENT_POINTS && ImGui::SmallButton("Add stop"))
    {
        // Seed the new stop at the top of the gradient.
        pc->gradient[pc->gradientCount] = pc->gradient[pc->gradientCount - 1];
        pc->gradient[pc->gradientCount].height = 1.0f;
        pc->gradientCount++;
    }
    if (pc->gradientCount > 1)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove stop"))
            pc->gradientCount--;
    }
}
} // namespace bagel
