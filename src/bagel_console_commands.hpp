#pragma once
#include "first_app.hpp"
//#include "render_systems/ecs_model_render_system.hpp"
namespace bagel {
namespace ConsoleCommand {
	//Console Callbacks
	//The console will call Addlog() with the returned char*

	// FirstApp Controllers
	char* ToggleFly(void* ptr) {
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->freeFly = !app->freeFly;
		if (app->freeFly) return "Free fly acivated";
		else return "Free fly deacivated";
	}
	char* TogglePhys(void* ptr) {
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->runPhys = !app->runPhys;
		if (app->runPhys) return "Physics acivated";
		else return "Physics deacivated";
	}
	char* RotateLight(void* ptr) {
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->rotateLight = !app->rotateLight;
		if (app->rotateLight) return "Rotating Light acivated";
		else return "Rotating Light deacivated";
	}
	char* ShowFPS(void* ptr)
	{
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->showFPS = !app->showFPS;
		if (app->showFPS) return "Printing FPS. FPS will tank";
		else return "Printing FPS";
	}
	char* ShowInfo(void* ptr)
	{
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->showInfo = !app->showInfo;
		if (app->showInfo) return "Displaying debug info of all entities";
		else return "Stopped displaying debug info";
	}
	char* ShowWireframe(void* ptr) 
	{
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->showWireframe = !app->showWireframe;
		if (app->showWireframe) return "Enabled wireframe renderer";
		else return "Disabled wireframe renderer";
	}
}
}