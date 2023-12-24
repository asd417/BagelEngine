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
}
}