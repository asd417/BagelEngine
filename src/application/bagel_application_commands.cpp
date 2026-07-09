#include "bagel_application.hpp"
#include "bagel_console_commands.hpp"
#include "bagel_util.hpp"
#include "imgui/bagel_imgui.hpp"

namespace bagel {
    void Application::initCommand()
	{
		CONSOLE->AddCommand("FREEFLY", this, ConsoleCommand::ToggleFly);
		CONSOLE->AddCommand("TOGGLEPHYSICS", this, ConsoleCommand::TogglePhys);
		CONSOLE->AddCommand("SHOWINFO", this, ConsoleCommand::ShowInfo);
		CONSOLE->AddCommand("SHOWWIREFRAME", this, ConsoleCommand::ShowWireframe);
		CONSOLE->AddCommand("R_DRAWBBOX", this, ConsoleCommand::DrawBBox);
		CONSOLE->AddCommand("PROFILE", this, ConsoleCommand::ShowProfile);
		CONSOLE->AddCommandWithArg("R_DRAWMODE", this, ConsoleCommand::SetDrawMode);
		CONSOLE->AddCommandWithArg("R_DRAWBLOOM", this, ConsoleCommand::SetBloom);
		CONSOLE->AddCommandWithArg("R_MAXFPS", this, ConsoleCommand::SetMaxFPS);
		CONSOLE->AddCommandWithArg("R_VSYNC", this, ConsoleCommand::SetVSync);
		CONSOLE->AddCommandWithArg("SKIN", this, ConsoleCommand::SetSkin);
		CONSOLE->AddCommandWithArg("R_MIPBIAS", this, ConsoleCommand::SetMipBias);
		CONSOLE->AddCommand("R_SMAA", this, ConsoleCommand::ToggleSmaa);
		CONSOLE->AddCommandWithArg("EDITMODE", this, ConsoleCommand::SetEditMode);
		CONSOLE->AddCommandWithArg("MAP", this, ConsoleCommand::LoadMap);
		// Keybinds (Source-style): bind/unbind any console command to a key. No default binds —
		// the grave/UI-toggle stays hard-coded in run() so it's always available. (TOGGLEUI is
		// still registered so it can be bound to a *different* key if desired.)
		CONSOLE->AddCommand("TOGGLEUI", this, ConsoleCommand::ToggleUI);
		CONSOLE->AddCommandWithArg("BIND", this, ConsoleCommand::Bind);
		CONSOLE->AddCommandWithArg("UNBIND", this, ConsoleCommand::Unbind);
		CONSOLE->AddCommand("UNBINDALL", this, ConsoleCommand::UnbindAll);

		// Persist binds across runs: a Source-style config that's re-exec'd on startup. Binds
		// auto-save on every change; load replays the file now that BIND is registered.
		keybinds.setConfigPath(util::enginePath("/keybinds.cfg"));
		keybinds.load([](const char *cmd)
					  { ConsoleApp::Instance()->Run(cmd); });
	}
}