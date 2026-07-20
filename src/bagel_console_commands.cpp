#include "bagel_console_commands.hpp"

// Console command callbacks. Declared in bagel_console_commands.hpp; defined here (out of the
// header) so they have a single definition ??multiple .cpp files register/reference them, and
// header-defined non-inline functions would collide at link time (LNK2005).
namespace bagel {
namespace ConsoleCommand {

	// FirstApp Controllers
	const char* ToggleFly(void* ptr) {
		Application* app = static_cast<Application*>(ptr);
		app->freeFly = !app->freeFly;
		if (app->freeFly) return "Free fly acivated";
		else return "Free fly deacivated";
	}
	const char* TogglePhys(void* ptr) {
		Application* app = static_cast<Application*>(ptr);
		app->runPhys = !app->runPhys;
		if (app->runPhys) return "Physics acivated";
		else return "Physics deacivated";
	}

	const char* ShowInfo(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->showInfo = !app->showInfo;
		if (app->showInfo) return "Displaying debug info of all entities";
		else return "Stopped displaying debug info";
	}
	const char* ShowWireframe(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->showWireframe = !app->showWireframe;
		if (app->showWireframe) return "Enabled wireframe renderer";
		else return "Disabled wireframe renderer";
	}
	const char* DrawBBox(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->drawBBox = !app->drawBBox;
		if (app->drawBBox) return "r_drawbbox: on";
		else return "r_drawbbox: off";
	}
	const char* ShowProfile(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->showProfile = !app->showProfile;
		if (app->showProfile) return "Profiling enabled ??printing section timings every second";
		else return "Profiling disabled";
	}
	const char* SetBloom(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') {
			snprintf(response, sizeof(response), "r_drawbloom: %d", (int)app->bloomEnabled);
			return response;
		}
		app->bloomEnabled = atoi(args) != 0;
		snprintf(response, sizeof(response), "Bloom %s", app->bloomEnabled ? "enabled" : "disabled");
		return response;
	}
	const char* SetVSync(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') {
			snprintf(response, sizeof(response), "r_vsync: %d", (int)app->vsync);
			return response;
		}
		bool enabled = atoi(args) != 0;
		app->vsync = enabled;
		app->vsyncDirty = true;
		snprintf(response, sizeof(response), "VSync %s", enabled ? "enabled" : "disabled");
		return response;
	}
	const char* SetMaxFPS(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') {
			if (app->maxFps == 0) snprintf(response, sizeof(response), "r_maxfps: unlimited");
			else                  snprintf(response, sizeof(response), "r_maxfps: %d", app->maxFps);
			return response;
		}
		int v = atoi(args);
		if (v == 0) {
			app->maxFps = 0;
			return "FPS limit removed";
		}
		if (v < 15) {
			snprintf(response, sizeof(response), "[error] r_maxfps: minimum is 15 (got %d)", v);
			return response;
		}
		app->maxFps = v;
		snprintf(response, sizeof(response), "FPS limited to %d", v);
		return response;
	}
	// skin <n>  ??set the skin index on every ModelComponent (clamped per-model to numSkins)
	const char* SetSkin(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') return "skin <n>: set skin index on all models";
		int idx = atoi(args);
		if (idx < 0) idx = 0;
		int count = 0;
		for (auto [e, m] : app->getRegistry().view<ModelComponent>().each()) {
			m.setSkin(static_cast<uint32_t>(idx));
			++count;
		}
		snprintf(response, sizeof(response), "Set skin %d on %d model(s)", idx, count);
		return response;
	}
	// r_mipbias <f>  ??shared texture sampler mip LOD bias. Negative = sharper (mips farther,
	// more shimmer), positive = blurrier (mips closer). Retunes all loaded content textures live.
	const char* SetMipBias(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0')
			return "r_mipbias <f>: sampler mip LOD bias (negative=sharper, positive=blurrier)";
		float bias = static_cast<float>(atof(args));
		app->setTextureMipBias(bias);
		snprintf(response, sizeof(response), "r_mipbias set to %.2f", bias);
		return response;
	}
	const char* ToggleSmaa(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->smaaEnabled = !app->smaaEnabled;
		return app->smaaEnabled ? "SMAA enabled" : "SMAA disabled (passthrough)";
	}
	// map <name>  ??load /maps/<name>.bmap by name (unloads current scene + rehydrates GPU state).
	const char* LoadMap(void* ptr, const char* args)
	{
		static char response[256];
		Application* app = static_cast<Application*>(ptr);
		const std::string msg = app->consoleLoadMap(args ? args : "");
		snprintf(response, sizeof(response), "%s", msg.c_str());
		return response;
	}
	// textmap <name>  -- build /maps/<name>.yaml (human-readable static map) into the live scene.
	const char* LoadTextMap(void* ptr, const char* args)
	{
		static char response[256];
		Application* app = static_cast<Application*>(ptr);
		const std::string msg = app->consoleLoadTextMap(args ? args : "");
		snprintf(response, sizeof(response), "%s", msg.c_str());
		return response;
	}
	// editmode <0|1>  ??toggle the bone-posing gizmo edit mode (same as the G hotkey).
	const char* SetEditMode(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') {
			snprintf(response, sizeof(response), "editmode: %d", (int)app->gizmoEditModeOn());
			return response;
		}
		bool on = atoi(args) != 0;
		app->setGizmoEditMode(on);
		snprintf(response, sizeof(response), "Pose edit mode %s", on ? "ON" : "OFF");
		return response;
	}
	// r_drawmode <n>  ??0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic
	//                   6=bloom 7=raw emission 8=raw radiosity 9=SMAA edges 10=SMAA weights
	const char* SetDrawMode(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') {
			snprintf(response, sizeof(response),
				"r_drawmode: current mode is %d (0-10)", app->gbufferDebugMode);
			return response;
		}
		int mode = atoi(args);
		if (mode < 0 || mode > 10) {
			snprintf(response, sizeof(response),
				"[error] r_drawmode: invalid mode %d (valid: 0-10)", mode);
			return response;
		}
		app->gbufferDebugMode = mode;
		static const char* names[] = {
			"Final composite", "Albedo", "World normals",
			"World position",  "Roughness", "Metallic", "Bloom", "Raw emission",
			"Raw radiosity", "SMAA edges", "SMAA weights"
		};
		snprintf(response, sizeof(response), "GBuffer view: %s", names[mode]);
		return response;
	}

	// ---- Keybinds (Source-style) -------------------------------------------
	// bind <key> <command...>  ??run a console command on key press. With no args, lists binds.
	//   e.g.  bind G EDITMODE 1     bind F5 R_DRAWMODE 1     bind GRAVE TOGGLEUI
	const char* Bind(void* ptr, const char* args)
	{
		static char response[256];
		Application* app = static_cast<Application*>(ptr);
		std::string a = args ? args : "";
		// trim leading spaces
		size_t b = a.find_first_not_of(' ');
		if (b == std::string::npos) {
			auto binds = app->getKeybinds().list();
			if (binds.empty()) return "bind <key> <command>: no binds set";
			static char listBuf[1024];
			int n = snprintf(listBuf, sizeof(listBuf), "%d bind(s):", (int)binds.size());
			for (auto& kv : binds) {
				if (n >= (int)sizeof(listBuf) - 1) break;
				n += snprintf(listBuf + n, sizeof(listBuf) - n, "\n  %s -> %s", kv.first.c_str(), kv.second.c_str());
			}
			return listBuf;
		}
		a = a.substr(b);
		const size_t sp = a.find(' ');
		if (sp == std::string::npos)
			return "bind <key> <command>: specify a command to bind";
		const std::string key = a.substr(0, sp);
		std::string cmd = a.substr(sp + 1);
		if (size_t c = cmd.find_first_not_of(' '); c != std::string::npos) cmd = cmd.substr(c);
		if (!app->getKeybinds().bind(key, cmd)) {
			snprintf(response, sizeof(response), "[error] bind: unknown key '%s'", key.c_str());
			return response;
		}
		snprintf(response, sizeof(response), "Bound %s -> %s", key.c_str(), cmd.c_str());
		return response;
	}
	// unbind <key>
	const char* Unbind(void* ptr, const char* args)
	{
		static char response[128];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') return "unbind <key>";
		std::string key(args);
		if (size_t sp = key.find(' '); sp != std::string::npos) key = key.substr(0, sp); // first token only
		if (!app->getKeybinds().unbind(key)) {
			snprintf(response, sizeof(response), "[error] unbind: unknown key '%s'", key.c_str());
			return response;
		}
		snprintf(response, sizeof(response), "Unbound %s", key.c_str());
		return response;
	}
	// unbindall
	const char* UnbindAll(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->getKeybinds().unbindAll();
		return "All keybinds cleared";
	}
	// toggleui ??show/hide all ImGui panels (default-bound to GRAVE). Bindable like any command.
	const char* ToggleUI(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->showImgui = !app->showImgui;
		return app->showImgui ? "UI shown" : "UI hidden";
	}
}
}
