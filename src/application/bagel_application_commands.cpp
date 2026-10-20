#include "bagel_application.hpp"
#include "bagel_console_commands.hpp"
#include "bagel_util.hpp"
#include "imgui/bagel_imgui.hpp"

#include <filesystem>
#include <sstream>

namespace bagel {

	// `anim` console command. Kept here next to the command registration rather than in
	// bagel_application.cpp, which is already the frame loop and nothing else.
	std::string Application::consoleAnim(const std::string &args)
	{
		// Trim: the console hands over the raw remainder of the line.
		const size_t a = args.find_first_not_of(" \t");
		const std::string arg = (a == std::string::npos) ? std::string{} : args.substr(a, args.find_last_not_of(" \t") - a + 1);

		const std::string animDir = util::enginePath("/anims");

		if (arg.empty() || arg == "list") {
			std::ostringstream os;
			const std::vector<std::string> run = animPlayer.running();
			os << "running: ";
			if (run.empty()) os << "(none)";
			else for (size_t i = 0; i < run.size(); ++i) os << (i ? ", " : "") << run[i];
			os << "\navailable in " << animDir << ": ";
			std::error_code ec;
			bool any = false;
			for (const auto &f : std::filesystem::directory_iterator(animDir, ec)) {
				if (f.path().extension() != ".yaml") continue;
				os << (any ? ", " : "") << f.path().stem().string();
				any = true;
			}
			if (!any) os << "(none)";
			return os.str();
		}

		if (arg.rfind("stop", 0) == 0) {
			const size_t s = arg.find_first_not_of(" \t", 4);
			if (s == std::string::npos) {
				animPlayer.stopAll();
				return "anim: stopped all sequences";
			}
			const std::string name = arg.substr(s);
			animPlayer.stop(name);
			return "anim: stopped '" + name + "'";
		}

		if (arg == "props") {
			// The authoring reference: these keys are exactly what a track's `property:` takes.
			std::ostringstream os;
			os << "animatable properties:";
			for (const AnimatableProperty &p : animatableProperties())
				os << "\n  " << p.key() << "  (" << p.arity() << (p.arity() == 1 ? " value)" : " values)");
			return os.str();
		}

		const std::string path = animDir + "/" + arg + ".yaml";
		std::string msg;
		if (!animPlayer.play(path, arg, msg))
			return "[error] anim " + arg + ": " + msg;
		return "anim: " + msg;
	}

    void Application::initCommand()
	{
		CONSOLE->AddCommand("FREEFLY", this, ConsoleCommand::ToggleFly);
		CONSOLE->AddCommandWithArg("ORBIT", this, ConsoleCommand::SetOrbitCamera);
		CONSOLE->AddCommand("CAMFOCUS", this, ConsoleCommand::CamFocus);
		CONSOLE->AddCommandWithArg("SETPOS", this, ConsoleCommand::SetPos);
		CONSOLE->AddCommandWithArg("SETANG", this, ConsoleCommand::SetAng);
		CONSOLE->AddCommand("GETPOS", this, ConsoleCommand::GetPos);
		CONSOLE->AddCommand("ENT_LIST", this, ConsoleCommand::EntList);
		CONSOLE->AddCommandWithArg("SCREENSHOT", this, ConsoleCommand::Screenshot);
		CONSOLE->AddCommandWithArg("ANIM", this, ConsoleCommand::Anim);
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
		CONSOLE->AddCommandWithArg("TEXTMAP", this, ConsoleCommand::LoadTextMap);
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