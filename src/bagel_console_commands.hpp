#pragma once
#include "application/bagel_application.hpp"
#include <cstdlib>
#include <string>
namespace bagel {
namespace ConsoleCommand {
	// Console Callbacks. The console calls these and AddLog()s the returned char*.
	// Definitions live in bagel_console_commands.cpp (out of this header so the bodies have a
	// single definition ??several .cpp files reference them, and header-defined non-inline
	// functions would collide at link time).

	// FirstApp Controllers
	const char* ToggleFly(void* ptr);
	const char* TogglePhys(void* ptr);
	const char* ShowInfo(void* ptr);
	const char* ShowWireframe(void* ptr);
	const char* DrawBBox(void* ptr);
	const char* ShowProfile(void* ptr);
	const char* SetBloom(void* ptr, const char* args);
	const char* SetVSync(void* ptr, const char* args);
	const char* SetMaxFPS(void* ptr, const char* args);
	// skin <n>  ??set the skin index on every ModelComponent (clamped per-model to numSkins)
	const char* SetSkin(void* ptr, const char* args);
	// r_mipbias <f>  ??shared texture sampler mip LOD bias (negative=sharper, positive=blurrier)
	const char* SetMipBias(void* ptr, const char* args);
	const char* ToggleSmaa(void* ptr);
	// map <name>  ??load /maps/<name>.bmap by name (unloads current scene + rehydrates GPU state).
	const char* LoadMap(void* ptr, const char* args);
	// editmode <0|1>  ??toggle the bone-posing gizmo edit mode (same as the G hotkey).
	const char* SetEditMode(void* ptr, const char* args);
	// r_drawmode <n>  ??0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic
	//                   6=bloom 7=raw emission 8=raw radiosity 9=SMAA edges 10=SMAA weights
	const char* SetDrawMode(void* ptr, const char* args);

	// ---- Keybinds (Source-style) -------------------------------------------
	// bind <key> <command...>  ??run a console command on key press. With no args, lists binds.
	const char* Bind(void* ptr, const char* args);
	// unbind <key>
	const char* Unbind(void* ptr, const char* args);
	// unbindall
	const char* UnbindAll(void* ptr);
	// toggleui ??show/hide all ImGui panels (default-bound to GRAVE). Bindable like any command.
	const char* ToggleUI(void* ptr);
}
}
