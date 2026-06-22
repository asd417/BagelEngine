#pragma once
#include "imgui.h"

#include <ctype.h>          // toupper
#include <limits.h>         // INT_MIN, INT_MAX
#include <math.h>           // sqrtf, powf, cosf, sinf, floorf, ceilf
#include <stdio.h>          // vsnprintf, sscanf, printf
#include <stdlib.h>         // NULL, malloc, free, atoi
#include <stdint.h>         // intptr_t
#include <map>
#include <unordered_map>
#include <functional>

#include "entt.hpp"
#include <glm/glm.hpp>
#include "bagel_ecs_components.hpp"

namespace bagel {

    char* ClearConsole(void* ptr);
    char* HelpCommand(void* ptr);
    char* HistoryCommand(void* ptr);

    //Shows various infos of entities.
    //Currently only shows World Position and World Rotation
    inline void DrawInfoPanels(entt::registry& registry, uint32_t extentWidth, uint32_t extentHeight, glm::mat4 projectionMat, glm::mat4 viewMat) {
        ImGuiWindowFlags window_flags = 0;
        //window_flags |= ImGuiWindowFlags_NoTitleBar;
        window_flags |= ImGuiWindowFlags_NoScrollbar;
        window_flags |= ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoResize;
        window_flags |= ImGuiWindowFlags_NoCollapse;
        window_flags |= ImGuiWindowFlags_NoNav;
        window_flags |= ImGuiWindowFlags_NoBackground;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
        window_flags |= ImGuiWindowFlags_NoMouseInputs;
        window_flags |= ImGuiWindowFlags_NoDecoration;

        auto view = registry.view<InfoComponent, TransformComponent>();
        uint32_t infoPanelID = 0;
        std::string panelName = "InfoPanel";
        for (auto [ent, info, trans] : view.each()) {
            glm::vec3 pos = trans.getWorldTranslation();
            glm::vec4 screenSpace = projectionMat * viewMat * glm::vec4(pos, 1.0);
            glm::vec3 normalizedDeviceCoordinate = { screenSpace.x / screenSpace.w,screenSpace.y / screenSpace.w, screenSpace.z / screenSpace.w };
            if (screenSpace.z < 0) {
                continue;
            }
            float screenX = (normalizedDeviceCoordinate.x + 1.0f) * 0.5f * extentWidth;
            float screenY = (normalizedDeviceCoordinate.y + 1.0f) * 0.5f * extentHeight;

            ImGui::Begin((panelName + std::to_string(infoPanelID)).c_str(), nullptr, window_flags);

            ImGui::SetWindowPos({ screenX,screenY }, ImGuiCond_Always);
            ImGui::SetWindowSize({ 500,500 }, ImGuiCond_Once);
            glm::vec3 p = trans.getWorldTranslation();
            ImGui::Text("Position (%.2f) (%.2f) (%.2f)", p.x, p.y, p.z);
            ImGui::Text("Rotation (%.2f) (%.2f) (%.2f)", trans.getWorldRotation().x, trans.getWorldRotation().y, trans.getWorldRotation().z);
            ImGui::End();
            infoPanelID++;
        }
    }

    // Live registry inspector: lists every entity and the components it carries, with
    // a few editable fields. Read-mostly debug view of the ECS world.
    inline void DrawRegistryPanel(entt::registry& registry) {
        ImGui::Begin("Registry");

        int total = 0;
        for (auto e : registry.storage<entt::entity>()) { (void)e; ++total; }
        ImGui::Text("Entities: %d", total);
        ImGui::Separator();

        auto drawModel = [](const char* label, ModelComponent& m) {
            ImGui::Text("%s: \"%s\"", label, m.loadSettings.source.c_str());
            ImGui::Text("  submeshes=%u  indexCount=%u  vertexCount=%u", m.submeshCount, m.indexCount, m.vertexCount);
            ImGui::Text("  ownsBuffers=%s  frustumCull=%s  matSources=%d",
                m.ownsBuffers ? "yes" : "no", m.frustumCull ? "yes" : "no", (int)m.materialCount);
        };

        for (auto entity : registry.storage<entt::entity>()) {
            // Show the recycled index (low bits) rather than the packed identifier, whose high
            // bits hold the version that climbs every time a slot is destroyed and reused.
            const uint32_t idx = static_cast<uint32_t>(entt::entt_traits<entt::entity>::to_entity(entity));
            const uint32_t ver = static_cast<uint32_t>(entt::entt_traits<entt::entity>::to_version(entity));
            ImGui::PushID(static_cast<int>(entt::to_integral(entity))); // unique across versions
            if (ImGui::TreeNode("ent", "Entity %u (v%u)", idx, ver)) {
                if (registry.all_of<Transient>(entity))
                    ImGui::TextColored(ImVec4(1.f, 0.7f, 0.2f, 1.f), "[Transient] (excluded from maps)");

                if (auto* t = registry.try_get<TransformComponent>(entity)) {
                    glm::vec3 p = t->getTranslation();
                    if (ImGui::DragFloat3("Translation", &p.x, 0.05f)) t->setTranslation(p);
                    glm::vec3 s = t->getScale();
                    if (ImGui::DragFloat3("Scale", &s.x, 0.01f)) t->setScale(s);
                }
                if (auto* ta = registry.try_get<TransformArrayComponent>(entity))
                    ImGui::Text("TransformArray: %u instances, buffered=%s", ta->count(), ta->useBuffer() ? "yes" : "no");
                if (auto* h = registry.try_get<TransformHierachyComponent>(entity))
                    ImGui::Text("Hierarchy: parent=%u depth=%u",
                        h->hasParent ? (uint32_t)entt::to_integral(h->parent) : 0u, h->depth);
                if (auto* pl = registry.try_get<PointLightComponent>(entity)) {
                    ImGui::ColorEdit3("PointLight color", &pl->color.x);
                    ImGui::DragFloat("PointLight lux", &pl->lux, 5.0f, 0.0f, 50000.0f);
                }
                if (auto* dl = registry.try_get<DirectionalLightComponent>(entity)) {
                    ImGui::ColorEdit3("Sun color", &dl->color.x);
                    ImGui::DragFloat3("Sun rotation", &dl->rotation.x, 0.5f);
                    ImGui::DragFloat("Sun lux", &dl->lux, 10.0f, 0.0f, 100000.0f);
                }
                if (auto* m = registry.try_get<ModelComponent>(entity))          drawModel("Model", *m);
                if (auto* a = registry.try_get<AnimationComponent>(entity))
                {
                    ImGui::Text("Animation: joints=%u  clips=%u  fps=%.1f",
                        a->jointCount, a->clipCount(), a->fps);
                    ImGui::Text("paletteBase=%u  dynamicBase=%u", a->paletteBase, a->dynamicPaletteBase);

                    if (ImGui::Checkbox("Manual pose", &a->manualPose)) a->poseDirty = true;
                    if (a->manualPose) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Reset to rest")) {
                            a->editPose = a->skeleton.restPose;
                            a->poseDirty = true;
                        }
                        ImGui::Text("editPose joints=%u  dirty=%s",
                            (unsigned)a->editPose.size(), a->poseDirty ? "yes" : "no");
                    } else if (a->clipCount() > 0) {
                        // Clip dropdown: pick any loaded glTF animation by name and switch live.
                        if (ImGui::BeginCombo("Clip", a->clipName(a->clip))) {
                            for (uint32_t i = 0; i < a->clipCount(); ++i) {
                                ImGui::PushID((int)i); // distinct IDs for same-named/unnamed clips
                                const bool selected = (i == a->clip);
                                if (ImGui::Selectable(a->clipName(i), selected)) {
                                    a->clip = i;
                                    a->time = 0.0f;
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                                ImGui::PopID();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::Button(a->playing ? "Pause" : "Play")) a->playing = !a->playing;
                        ImGui::SameLine();
                        if (ImGui::Button("Restart")) { a->time = 0.0f; a->playing = true; }
                        ImGui::SameLine();
                        ImGui::Checkbox("Loop", &a->loop);
                        const float dur = a->clipDuration(a->clip);
                        if (ImGui::SliderFloat("Time", &a->time, 0.0f, dur > 0.0f ? dur : 1.0f))
                            a->playing = false; // scrubbing pauses playback
                    }

                    // ---- IK setups (applied on top of editPose while Manual pose is on) ----
                    ImGui::Separator();
                    ImGui::Text("IK setups (active while Manual pose is on)");
                    auto boneCombo = [&](const char* label, int& ref) {
                        const char* cur = "(none)";
                        if (ref >= 0 && ref < (int)a->skeleton.names.size())
                            cur = a->skeleton.names[ref].empty() ? "(unnamed)" : a->skeleton.names[ref].c_str();
                        if (ImGui::BeginCombo(label, cur)) {
                            if (ImGui::Selectable("(none)", ref < 0)) ref = -1;
                            for (int j = 0; j < (int)a->jointCount; ++j) {
                                ImGui::PushID(j);
                                const char* nm = (j < (int)a->skeleton.names.size() && !a->skeleton.names[j].empty())
                                    ? a->skeleton.names[j].c_str() : "(unnamed)";
                                if (ImGui::Selectable(nm, j == ref)) ref = j;
                                ImGui::PopID();
                            }
                            ImGui::EndCombo();
                        }
                    };
                    int ikRemove = -1;
                    for (size_t i = 0; i < a->ikSetups.size(); ++i) {
                        ImGui::PushID((int)(1000 + i));
                        IKSetup& s = a->ikSetups[i];
                        ImGui::Text("IK %d", (int)i);
                        ImGui::Checkbox("Enabled", &s.enabled);
                        boneCombo("Thigh", s.thigh);
                        boneCombo("Shin",  s.shin);
                        boneCombo("Foot",  s.foot);
                        boneCombo("Goal", s.goalJoint);
                        boneCombo("Pole", s.poleJoint);
                        ImGui::SliderFloat("Weight", &s.weight, 0.0f, 1.0f);
                        if (ImGui::SmallButton("Remove")) ikRemove = (int)i;
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if (ikRemove >= 0) a->ikSetups.erase(a->ikSetups.begin() + ikRemove);
                    if (ImGui::Button("Add IK setup")) a->ikSetups.push_back(IKSetup{});
                }
                if (auto* w = registry.try_get<WireframeComponent>(entity))      drawModel("Wireframe", *w);
                if (auto* c = registry.try_get<CollisionModelComponent>(entity)) drawModel("CollisionModel", *c);
                if (auto* db = registry.try_get<DataBufferComponent>(entity))
                    ImGui::Text("DataBuffer: handle=%u", db->getBufferHandle());
                if (registry.all_of<JoltPhysicsComponent>(entity))   ImGui::TextUnformatted("JoltPhysics");
                if (registry.all_of<JoltKinematicComponent>(entity)) ImGui::TextUnformatted("JoltKinematic");
                if (registry.all_of<InfoComponent>(entity))          ImGui::TextUnformatted("Info");

                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    class ConsoleApp
    {
    public:
        static ConsoleApp* Instance() {
            if (instance == nullptr) {
                instance = new ConsoleApp();
            }
            return instance;
        }

        void Log(std::string callerName, std::string message) {
            std::cout << callerName << ": " << message << "\n";
            
            AddLog((callerName + ": " + message).c_str());
        }

        void AddLog(const char* fmt, ...) IM_FMTARGS(2)
        {
            // FIXME-OPT
            char buf[1024];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
            buf[IM_ARRAYSIZE(buf) - 1] = 0;
            va_end(args);
            Items.push_back(Strdup(buf));
        }

        void AddCommand(const char* name, void* obj, std::function<char*(void*)> callback) {
            Commands.push_back(name);
            callbackMap.emplace(name, std::pair{ obj, callback});
        }

        void AddCommandWithArg(const char* name, void* obj, std::function<char*(void*, const char*)> callback) {
            Commands.push_back(name);
            callbackMapWithArgs.emplace(name, std::pair{ obj, callback });
        }

        // Execute a command line programmatically (e.g. from a keybind). Unlike typing into
        // the console, this does NOT echo the input line or push to history — only the
        // command's own response is logged. Returns true if a command matched.
        bool Run(const char* command_line) { return Dispatch(command_line); }

        void ClearLog()
        {
            for (int i = 0; i < Items.Size; i++)
                free(Items[i]);
            Items.clear();
        }

        void Draw(const char* title, bool* p_open)
        {
            ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin(title, p_open))
            {
                ImGui::End();
                return;
            }

            // As a specific feature guaranteed by the library, after calling Begin() the last Item represent the title bar.
            // So e.g. IsItemHovered() will return true when hovering the title bar.
            // Here we create a context menu only available from the title bar.
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Close Console"))
                    *p_open = false;
                ImGui::EndPopup();
            }

            if (ImGui::SmallButton("Clear")) { ClearLog(); }
            ImGui::SameLine();
            if (ImGui::BeginPopup("Options"))
            {
                ImGui::Checkbox("Auto-scroll", &AutoScroll);
                ImGui::EndPopup();
            }
            if (ImGui::SmallButton("Options"))
                ImGui::OpenPopup("Options");
            ImGui::SameLine();
            Filter.Draw("Search", 180);
            ImGui::Separator();

            // Reserve enough left-over height for 1 separator + 1 input text
            const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
            if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
            {
                if (ImGui::BeginPopupContextWindow())
                {
                    if (ImGui::Selectable("Clear")) ClearLog();
                    ImGui::EndPopup();
                }

                // Display every line as a separate entry so we can change their color or add custom widgets.
                // If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
                // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
                // to only process visible items. The clipper will automatically measure the height of your first item and then
                // "seek" to display only items in the visible area.
                // To use the clipper we can replace your standard loop:
                //      for (int i = 0; i < Items.Size; i++)
                //   With:
                //      ImGuiListClipper clipper;
                //      clipper.Begin(Items.Size);
                //      while (clipper.Step())
                //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                // - That your items are evenly spaced (same height)
                // - That you have cheap random access to your elements (you can access them given their index,
                //   without processing all the ones before)
                // You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
                // We would need random-access on the post-filtered list.
                // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
                // or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
                // and appending newly elements as they are inserted. This is left as a task to the user until we can manage
                // to improve this example code!
                // If your items are of variable height:
                // - Split them into same height items would be simpler and facilitate random-seeking into your list.
                // - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
                for (const char* item : Items)
                {
                    if (!Filter.PassFilter(item)) continue;

                    // Normally you would store more information in your item than just a string.
                    // (e.g. make Items[] an array of structure, store color/type etc.)
                    ImVec4 color;
                    bool has_color = false;
                    if (strstr(item, "[error]")) { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }
                    else if (strncmp(item, "# ", 2) == 0) { color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
                    if (has_color)
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(item);
                    if (has_color)
                        ImGui::PopStyleColor();
                }
                // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
                // Using a scrollbar or mouse-wheel will take away from the bottom edge.
                if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
                    ImGui::SetScrollHereY(1.0f);
                ScrollToBottom = false;

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::Separator();

            // Command-line
            bool reclaim_focus = false;
            ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
            if (ImGui::InputText("Input", InputBuf, IM_ARRAYSIZE(InputBuf), input_text_flags, &TextEditCallbackStub, (void*)this))
            {
                char* s = InputBuf;
                Strtrim(s);
                if (s[0])
                    ExecCommand(s);
                strcpy_s(s, sizeof(InputBuf), "");
                reclaim_focus = true;
            }

            // Auto-focus on window apparition
            ImGui::SetItemDefaultFocus();
            if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
            ImGui::End();
        }

        char                  InputBuf[256];
        ImVector<char*>       Items;
        ImVector<const char*> Commands;
        ImVector<char*>       History;
        int                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
        ImGuiTextFilter       Filter;
        bool                  AutoScroll;
        bool                  ScrollToBottom;

    private:
        //Singleton
        static ConsoleApp* instance;

        std::unordered_map<const char*, std::pair<void*, std::function<char*(void*)>>> callbackMap;
        std::unordered_map<const char*, std::pair<void*, std::function<char*(void*, const char*)>>> callbackMapWithArgs;

        ConsoleApp()
        {
            //IMGUI_DEMO_MARKER("Examples/Console");
            ClearLog();
            memset(InputBuf, 0, sizeof(InputBuf));
            HistoryPos = -1;

            // "CLASSIFY" is here to provide the test case where "C"+[tab] completes to "CL" and display multiple matches.
            AddCommand("CLEAR", this, ClearConsole);
            AddCommand("HELP", this, HelpCommand);
            AddCommand("HISTORY", this, HistoryCommand);
            AutoScroll = true;
            ScrollToBottom = false;
            AddLog("Welcome to Dear ImGui!");
        }
        ~ConsoleApp()
        {
            ClearLog();
            for (int i = 0; i < History.Size; i++)
                free(History[i]);
        }

        // Portable helpers
        static int   stringCompare(const char* s1, const char* s2) { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
        static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
        static char* Strdup(const char* s) { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = malloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
        static void  Strtrim(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }
        void ExecCommand(const char* command_line)
        {
            AddLog("# %s\n", command_line);

            // Insert into history. First find match and delete it so it can be pushed to the back.
            // This isn't trying to be smart or optimal.
            HistoryPos = -1;
            for (int i = History.Size - 1; i >= 0; i--)
                if (stringCompare(History[i], command_line) == 0)
                {
                    free(History[i]);
                    History.erase(History.begin() + i);
                    break;
                }
            History.push_back(Strdup(command_line));

            Dispatch(command_line);

            // On command input, we scroll to bottom even if AutoScroll==false
            ScrollToBottom = true;
        }

        // Parse `command_line` into name + optional argument and invoke the matching command,
        // logging its response. Shared by the console input (ExecCommand) and programmatic
        // execution (Run / keybinds). Returns true if a command matched.
        bool Dispatch(const char* command_line)
        {
            char cmdBuf[256];
            strncpy_s(cmdBuf, sizeof(cmdBuf), command_line, sizeof(cmdBuf) - 1);
            cmdBuf[sizeof(cmdBuf) - 1] = '\0';
            char* spacePos = strchr(cmdBuf, ' ');
            const char* args = "";
            if (spacePos) {
                *spacePos = '\0';
                args = spacePos + 1;
                while (*args == ' ') ++args; // trim leading spaces
            }

            bool ran = false;
            for (auto it = callbackMapWithArgs.begin(); it != callbackMapWithArgs.end(); it++) {
                if (stringCompare(cmdBuf, it->first) == 0) {
                    AddLog((it->second.second)(it->second.first, args));
                    ran = true;
                    break;
                }
            }
            if (!ran) {
                for (auto it = callbackMap.begin(); it != callbackMap.end(); it++) {
                    if (stringCompare(command_line, it->first) == 0) {
                        AddLog((it->second.second)(it->second.first));
                        ran = true;
                        break;
                    }
                }
            }
            if (!ran) AddLog("Unknown command: '%s'\n", command_line);
            return ran;
        }

        // In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
        static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
        {
            ConsoleApp* console = (ConsoleApp*)data->UserData;
            return console->TextEditCallback(data);
        }

        int TextEditCallback(ImGuiInputTextCallbackData* data)
        {
            //AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
            switch (data->EventFlag)
            {
            // Example of TEXT COMPLETION
            case ImGuiInputTextFlags_CallbackCompletion:
            {
                // Locate beginning of current word
                const char* word_end = data->Buf + data->CursorPos;
                const char* word_start = word_end;
                while (word_start > data->Buf)
                {
                    const char c = word_start[-1];
                    if (c == ' ' || c == '\t' || c == ',' || c == ';')
                        break;
                    word_start--;
                }

                // Build a list of candidates
                ImVector<const char*> candidates;
                for (int i = 0; i < Commands.Size; i++)
                    if (Strnicmp(Commands[i], word_start, (int)(word_end - word_start)) == 0)
                        candidates.push_back(Commands[i]);

                if (candidates.Size == 0)
                {
                    // No match
                    AddLog("No match for \"%.*s\"!\n", (int)(word_end - word_start), word_start);
                }
                else if (candidates.Size == 1)
                {
                    // Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
                    data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
                    data->InsertChars(data->CursorPos, candidates[0]);
                    data->InsertChars(data->CursorPos, " ");
                }
                else
                {
                    // Multiple matches. Complete as much as we can..
                    // So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
                    int match_len = (int)(word_end - word_start);
                    for (;;)
                    {
                        int c = 0;
                        bool all_candidates_matches = true;
                        for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
                            if (i == 0)
                                c = toupper(candidates[i][match_len]);
                            else if (c == 0 || c != toupper(candidates[i][match_len]))
                                all_candidates_matches = false;
                        if (!all_candidates_matches)
                            break;
                        match_len++;
                    }

                    if (match_len > 0)
                    {
                        data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
                        data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
                    }

                    // List matches
                    AddLog("Possible matches:\n");
                    for (int i = 0; i < candidates.Size; i++) AddLog("- %s\n", candidates[i]);
                }
                break;
            }
            case ImGuiInputTextFlags_CallbackHistory:
            {
                // Example of HISTORY
                const int prev_history_pos = HistoryPos;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (HistoryPos == -1)
                        HistoryPos = History.Size - 1;
                    else if (HistoryPos > 0)
                        HistoryPos--;
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (HistoryPos != -1)
                        if (++HistoryPos >= History.Size) HistoryPos = -1;
                }

                // A better implementation would preserve the data on the current input line along with cursor position.
                if (prev_history_pos != HistoryPos)
                {
                    const char* history_str = (HistoryPos >= 0) ? History[HistoryPos] : "";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, history_str);
                }
            }
            }
            return 0;
        }
    };
    
}
