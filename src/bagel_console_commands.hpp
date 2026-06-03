#pragma once
#include "first_app.hpp"
#include <cstdlib>
//#include "render_systems/ecs_model_render_system.hpp"
namespace bagel {
namespace ConsoleCommand {
	//Console Callbacks
	//The console will call Addlog() with the returned char*

	// FirstApp Controllers
	char* ToggleFly(void* ptr) {
		Application* app = static_cast<Application*>(ptr);
		app->freeFly = !app->freeFly;
		if (app->freeFly) return "Free fly acivated";
		else return "Free fly deacivated";
	}
	char* TogglePhys(void* ptr) {
		Application* app = static_cast<Application*>(ptr);
		app->runPhys = !app->runPhys;
		if (app->runPhys) return "Physics acivated";
		else return "Physics deacivated";
	}
char* ShowFPS(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->showFPS = !app->showFPS;
		if (app->showFPS) return "Printing FPS. FPS will tank";
		else return "Printing FPS";
	}
	char* ShowInfo(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->showInfo = !app->showInfo;
		if (app->showInfo) return "Displaying debug info of all entities";
		else return "Stopped displaying debug info";
	}
	char* ShowWireframe(void* ptr)
	{
		Application* app = static_cast<Application*>(ptr);
		app->showWireframe = !app->showWireframe;
		if (app->showWireframe) return "Enabled wireframe renderer";
		else return "Disabled wireframe renderer";
	}
	char* SetBloom(void* ptr, const char* args)
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
	// r_drawmode <n>  — 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission
	char* SetDrawMode(void* ptr, const char* args)
	{
		static char response[64];
		Application* app = static_cast<Application*>(ptr);
		if (!args || args[0] == '\0') {
			snprintf(response, sizeof(response),
				"r_drawmode: current mode is %d (0-7)", app->gbufferDebugMode);
			return response;
		}
		int mode = atoi(args);
		if (mode < 0 || mode > 7) {
			snprintf(response, sizeof(response),
				"[error] r_drawmode: invalid mode %d (valid: 0-7)", mode);
			return response;
		}
		app->gbufferDebugMode = mode;
		static const char* names[] = {
			"Final composite", "Albedo", "World normals",
			"World position",  "Roughness", "Metallic", "Bloom", "Raw emission"
		};
		snprintf(response, sizeof(response), "GBuffer view: %s", names[mode]);
		return response;
	}
}
}