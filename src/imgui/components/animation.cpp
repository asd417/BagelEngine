#include "imgui/components/component_panels.hpp"

#include "ecs/bagel_ecs_components.hpp"
#include "imgui.h"

namespace bagel
{
// Cold rig (a) + hot playback (play) are emplaced as a pair by the builder; the UI reads
// scalars/playback from `play` and skeleton/pose/tables from `a`.
void DrawAnimationComponent(entt::registry &registry, entt::entity entity)
{
    auto *a = registry.try_get<AnimationComponent>(entity);
    if (!a)
        return;
    auto *play = registry.try_get<AnimationPlaybackComponent>(entity);
    if (!play)
        return;

    ImGui::Text("Animation: joints=%u  clips=%u  fps=%.1f",
                play->jointCount, a->clipCount(), a->fps);
    ImGui::Text("paletteBase=%u  dynamicBase=%u", play->paletteBase, play->dynamicPaletteBase);

    if (ImGui::Checkbox("Manual pose", &play->manualPose))
        play->poseDirty = true;
    if (play->manualPose)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to rest"))
        {
            a->editPose = a->skeleton.restPose;
            play->poseDirty = true;
        }
        ImGui::Text("editPose joints=%u  dirty=%s",
                    (unsigned)a->editPose.size(), play->poseDirty ? "yes" : "no");
    }
    else if (a->clipCount() > 0)
    {
        // Clip dropdown: pick any loaded glTF animation by name and switch live.
        if (ImGui::BeginCombo("Clip", a->clipName(play->clip)))
        {
            for (uint32_t i = 0; i < a->clipCount(); ++i)
            {
                ImGui::PushID((int)i); // distinct IDs for same-named/unnamed clips
                const bool selected = (i == play->clip);
                if (ImGui::Selectable(a->clipName(i), selected))
                {
                    // selectClip refreshes the hot cached frame window from the cold
                    // tables — setting play->clip alone would leave animBaseOffset() stale.
                    selectClip(*play, *a, i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button(play->playing ? "Pause" : "Play"))
            play->playing = !play->playing;
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
        {
            play->time = 0.0f;
            play->playing = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &play->loop);
        const float dur = a->clipDuration(play->clip);
        if (ImGui::SliderFloat("Time", &play->time, 0.0f, dur > 0.0f ? dur : 1.0f))
            play->playing = false; // scrubbing pauses playback
    }

    // ---- IK setups (applied on top of editPose while Manual pose is on) ----
    ImGui::Separator();
    ImGui::Text("IK setups (active while Manual pose is on)");
    auto boneCombo = [&](const char *label, int &ref)
    {
        const char *cur = "(none)";
        if (ref >= 0 && ref < (int)a->skeleton.names.size())
            cur = a->skeleton.names[ref].empty() ? "(unnamed)" : a->skeleton.names[ref].c_str();
        if (ImGui::BeginCombo(label, cur))
        {
            if (ImGui::Selectable("(none)", ref < 0))
                ref = -1;
            for (int j = 0; j < (int)play->jointCount; ++j)
            {
                ImGui::PushID(j);
                const char *nm = (j < (int)a->skeleton.names.size() && !a->skeleton.names[j].empty())
                                     ? a->skeleton.names[j].c_str()
                                     : "(unnamed)";
                if (ImGui::Selectable(nm, j == ref))
                    ref = j;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    };
    int ikRemove = -1;
    for (size_t i = 0; i < a->ikSetups.size(); ++i)
    {
        ImGui::PushID((int)(1000 + i));
        IKSetup &s = a->ikSetups[i];
        ImGui::Text("IK %d", (int)i);
        ImGui::Checkbox("Enabled", &s.enabled);
        boneCombo("Thigh", s.thigh);
        boneCombo("Shin", s.shin);
        boneCombo("Foot", s.foot);
        boneCombo("Goal", s.goalJoint);
        boneCombo("Pole", s.poleJoint);
        ImGui::SliderFloat("Weight", &s.weight, 0.0f, 1.0f);
        if (ImGui::SmallButton("Remove"))
            ikRemove = (int)i;
        ImGui::Separator();
        ImGui::PopID();
    }
    if (ikRemove >= 0)
        a->ikSetups.erase(a->ikSetups.begin() + ikRemove);
    if (ImGui::Button("Add IK setup"))
        a->ikSetups.push_back(IKSetup{});
}
} // namespace bagel
