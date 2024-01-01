#pragma once
#include "first_app.hpp"
namespace bagel {
namespace ConsoleCommand {
	//Console Callbacks
	//The console will call Addlog() with the returned char*
	char* ToggleFly(void* ptr) {
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->freeFly = !app->freeFly;
		if (app->freeFly) return "Free fly acivated";
		else return "Free fly deacivated";
	}
	char* TogglePhys(void* ptr) {
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->runPhys = !app->runPhys;
		if (app->runPhys) return "Free fly acivated";
		else return "Free fly deacivated";
	}
	char* ResetScene(void* ptr) {
		FirstApp* app = static_cast<FirstApp*>(ptr);
		app->runPhys = false;
		app->resetScene();
		return "Reset scene and paused physics";
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
}
}