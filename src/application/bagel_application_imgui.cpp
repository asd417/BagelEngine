#include "bagel_application.hpp"
#include "imgui/bagel_imgui.hpp"
#include "keyboard_movement_controller.hpp"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui_internal.h" // DockBuilder* — the default dock layout below
namespace bagel
{
	void Application::initImgui()
	{
		VkDescriptorPoolSize pool_sizes[] =
			{
				{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
				{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
				{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
				{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000;
		pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
		pool_info.pPoolSizes = pool_sizes;

		VK_CHECK(vkCreateDescriptorPool(BGLDevice::device(), &pool_info, nullptr, &imguiPool));

		ImGui::CreateContext();
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		ImGui_ImplGlfw_InitForVulkan(bglWindow.getGLFWWindow(), true);

		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.ApiVersion = VK_API_VERSION_1_3;
		init_info.Instance = bglDevice.getInstance();
		init_info.PhysicalDevice = bglDevice.getPhysicalDevice();
		init_info.Device = BGLDevice::device();
		init_info.QueueFamily = bglDevice.findPhysicalQueueFamilies().graphicsFamily;
		init_info.Queue = bglDevice.graphicsQueue();
		init_info.DescriptorPool = imguiPool;
		init_info.MinImageCount = 3;
		init_info.ImageCount = 3;
		init_info.PipelineInfoMain.RenderPass = bglRenderer.getSwapChainRenderPass();
		init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

		ImGui_ImplVulkan_Init(&init_info);
	}

    void Application::drawImgui(BGLCamera &camera, KeyboardMovementController &cameraController, SmaaEdgeRenderSystem &edgeRender)
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		// Build panels only when the UI is toggled on. NewFrame/Render below always run so the
		// backend stays balanced; when hidden this just produces an empty (no-op) ImGui frame.
		if (showImgui)
		{
			// Host dockspace over the whole viewport so panels can be dragged to the edges. The central
			// node stays transparent and input-transparent while empty, so the 3D scene renders and
			// receives the mouse through it. Layout persists in imgui.ini next to the executable.
			const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

			// Default layout, built once on the first frame that has no layout to restore -- a fresh
			// checkout, or after deleting imgui.ini. When imgui.ini did supply one the root node comes
			// back already split, so this is skipped and the user's own arrangement survives.
			static bool dockLayoutChecked = false;
			if (!dockLayoutChecked)
			{
				dockLayoutChecked = true;
				const ImGuiDockNode *root = ImGui::DockBuilderGetNode(dockspaceId);
				if (root == nullptr || root->IsEmpty())
				{
					ImGui::DockBuilderRemoveNode(dockspaceId);
					ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
					ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

					// Each split carves off `center`, so the ratios apply to what is left of it, and
					// whatever remains at the end is the transparent central node the scene shows through.
					ImGuiID center = dockspaceId;
					const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
					const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
					const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);

					ImGui::DockBuilderDockWindow("Registry", left);
					ImGui::DockBuilderDockWindow("Maps", left); // tabbed with Registry
					ImGui::DockBuilderDockWindow("Settings", right);
					ImGui::DockBuilderDockWindow("Console", bottom);
					ImGui::DockBuilderFinish(dockspaceId);
				}
			}

			// Pose-gizmo edit-mode overlay, pinned to the top-left.
			if (poseGizmo.editModeOn())
			{
				const ImGuiWindowFlags ovFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking;
				ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
				ImGui::SetNextWindowBgAlpha(0.35f);
				ImGui::Begin("##editmode_overlay", nullptr, ovFlags);
				ImGui::TextUnformatted("Edit mode active. Press T for transform mode, R for rotation mode");
				ImGui::Text("Press L to toggle local/global space (now: %s)",
							poseGizmo.localSpaceOn() ? "local" : "global");
				ImGui::TextUnformatted("Shift+click bones to multi-select (batched transform/rotate)");
				if (poseGizmo.selectedJoint() >= 0)
				{
					const char *bn = poseGizmo.selectedJointName();
					const size_t nSel = poseGizmo.selectedJoints().size();
					if (nSel > 1)
						ImGui::Text("Active bone: %s  (+%d more selected)", (bn && bn[0]) ? bn : "(unnamed)", (int)nSel - 1);
					else
						ImGui::Text("Selected bone: %s", (bn && bn[0]) ? bn : "(unnamed)");
				}
				ImGui::End();
			}
			// Let the derived app draw its own panels and mutate the scene/registry
			// before anything reads it this frame (e.g. map build/load buttons).
			OnDrawGui();
			VkExtent2D ext = bglRenderer.getExtent();
			if (showInfo)
				DrawInfoPanels(registry, static_cast<uint16_t>(ext.width), static_cast<uint16_t>(ext.height), camera.getProjection(), camera.getView());
			ConsoleApp::Instance()->Draw("Console", nullptr);
			ImGui::Begin("Settings");
			// Draws a compact "Reset" button to the right of the preceding widget that restores
			// `v` to `def` — the factory default from bagel::cfg (bagel_engine_config.hpp), the same
			// constant that initializes the member, so the slider and its reset can't drift apart.
			// PushID(&v) keeps the repeated "Reset" labels unique without hand-authored string IDs.
			auto resetBtn = [](auto &v, auto def)
			{
				ImGui::SameLine();
				ImGui::PushID(&v);
				if (ImGui::SmallButton("Reset"))
					v = def;
				ImGui::PopID();
			};
			ImGui::SliderFloat("Mouse Sensitivity", &cameraController.mouseSensitivity, 0.05f, 0.3f);
			resetBtn(cameraController.mouseSensitivity, cfg::kMouseSensitivity);
			// Move speed: base 10 up to 100 (10x), log scale so the low end stays usable.
			ImGui::SliderFloat("Move Speed", &cameraController.moveSpeed, 1.0f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
			resetBtn(cameraController.moveSpeed, cfg::kMoveSpeed);
			ImGui::Separator();
			ImGui::Text("Camera");
			ImGui::SliderFloat("FOV (horizontal)", &cameraFovDegrees, 20.0f, 120.0f, "%.0f deg");
			resetBtn(cameraFovDegrees, cfg::kCameraFovDegrees);
			ImGui::SliderFloat("Near (close)", &cameraNear, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
			resetBtn(cameraNear, cfg::kCameraNear);
			ImGui::SliderFloat("Far", &cameraFar, 50.0f, 5000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
			resetBtn(cameraFar, cfg::kCameraFar);
			// Keep them sane and strictly ordered (near < far) so the projection stays valid.
			if (cameraNear < 0.001f)
				cameraNear = 0.001f;
			if (cameraFar <= cameraNear + 1.0f)
				cameraFar = cameraNear + 1.0f;
			ImGui::Separator();
			{
				auto dirView = registry.view<DirectionalLightComponent>();
				if (!dirView.empty())
				{
					auto &dlc = dirView.get<DirectionalLightComponent>(*dirView.begin());
					ImGui::Text("Shadow Cascades");
					ImGui::SliderFloat("Cascade 0 End", &dlc.cascadeEnds.x, 0.001f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
					resetBtn(dlc.cascadeEnds.x, cfg::kCascade0End);
					ImGui::SliderFloat("Cascade 1 End", &dlc.cascadeEnds.y, 0.001f, 30.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
					resetBtn(dlc.cascadeEnds.y, cfg::kCascade1End);
					ImGui::SliderFloat("Cascade 2 End", &dlc.cascadeEnds.z, 0.001f, 70.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
					resetBtn(dlc.cascadeEnds.z, cfg::kCascade2End);
					ImGui::SliderFloat("Cascade 3 End", &dlc.cascadeEnds.w, 0.001f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
					resetBtn(dlc.cascadeEnds.w, cfg::kCascade3End);
					// keep the splits strictly increasing
					// dlc.cascadeEnds.y = fmaxf(dlc.cascadeEnds.y, dlc.cascadeEnds.x + 0.1f);
					// dlc.cascadeEnds.z = fmaxf(dlc.cascadeEnds.z, dlc.cascadeEnds.y + 0.1f);
					// dlc.cascadeEnds.w = fmaxf(dlc.cascadeEnds.w, dlc.cascadeEnds.z + 0.1f);
					ImGui::SliderFloat("Caster Range", &dlc.casterRange, 10.0f, 2000.0f);
					resetBtn(dlc.casterRange, cfg::kCasterRange);
					ImGui::SliderFloat("Shadow Bias", &dlc.shadowBiasMin, 0.0f, 0.02f, "%.4f");
					resetBtn(dlc.shadowBiasMin, cfg::kShadowBiasMin);
					ImGui::SliderFloat("Shadow Bias Slope", &dlc.shadowBiasSlope, 0.0f, 0.05f, "%.4f");
					resetBtn(dlc.shadowBiasSlope, cfg::kShadowBiasSlope);
					ImGui::Separator();
				}
			}
			ImGui::Checkbox("Bloom", &bloomEnabled);
			if (bloomEnabled)
			{
				ImGui::SliderFloat("Bloom Intensity", &bloomIntensity, 0.0f, 2.0f);
				resetBtn(bloomIntensity, cfg::kBloomIntensity);
				ImGui::SliderFloat("Bloom Threshold", &bloomThreshold, 0.001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
				resetBtn(bloomThreshold, cfg::kBloomThreshold);
				ImGui::SliderFloat("Bloom Mip Decay", &bloomMipDecay, 0.5f, 1.0f);
				resetBtn(bloomMipDecay, cfg::kBloomMipDecay);
				ImGui::SliderFloat("Exposure", &exposure, 0.001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
				resetBtn(exposure, cfg::kExposure);
			}
			ImGui::Text("Water");
			// Opaque at this water depth (world units) when viewed from the reference distance.
			ImGui::SliderFloat("Water Opaque Depth", &waterOpaqueDepth, 0.1f, 64.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
			// Reference camera distance for the depth->opacity scaling: closer = more transparent.
			ImGui::SliderFloat("Water Cam Ref Dist", &waterCamRefDist, 1.0f, 1000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
			ImGui::Text("SMAA");
			// Edge detection method: luma (default), color (catches chroma edges, ~2x cost), or
			// depth (cheapest, geometry-only; wants a much smaller threshold — depth is non-linear).
			ImGui::Combo("SMAA Edge Method", &edgeRender.edgeMethod, "Predicated Luma\0Luma\0Color\0Depth\0Custom\0");
			resetBtn(edgeRender.edgeMethod, cfg::kSmaaEdgeMethod);
			// Log scale can't represent 0 — use a small positive min (like Bloom Threshold) or the
			// low end of the slider is unusable. SMAA's useful luma-threshold range is ~0.05-0.2.
			ImGui::SliderFloat("SMAA Threshold", &edgeRender.edgeThreshold, 0.001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
			resetBtn(edgeRender.edgeThreshold, cfg::kSmaaEdgeThreshold);
			// Unused by the depth method.
			ImGui::SliderFloat("SMAA Local Constrast Adaptation", &edgeRender.localConstrastAdapt, 1.0f, 4.0f, "%.4f");
			resetBtn(edgeRender.localConstrastAdapt, cfg::kSmaaLocalContrastAdapt);
			ImGui::End();
		} // showImgui — panels are built only when visible
		ImGui::Render();
	}
}