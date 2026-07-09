#pragma once
// -----------------------------------------------------------------------------
// Source-engine-style keybind system. A key -> console-command-string table that
// fires the bound command on key press (edge-detected) each frame. Bindings are
// authored via the `bind` / `unbind` / `unbindall` console commands and dispatched
// back through ConsoleApp::Run, so anything you can type in the console can be a
// hotkey — replacing scattered hard-coded glfwGetKey checks.
//
// GLFW is already included (with Vulkan) by bagel_window.hpp before this header is
// pulled in via bagel_application.hpp, so a plain include here is a no-op re-include.
// -----------------------------------------------------------------------------
#include <GLFW/glfw3.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <cctype>
#include <fstream>

namespace bagel {

	class KeyBindManager {
	public:
		// How load()/poll() run a bound command line — in practice ConsoleApp::Instance()->Run.
		// A plain function pointer: every call site passes a captureless lambda, which converts
		// implicitly, so there is nothing to type-erase.
		using ExecFn = void (*)(const char* commandLine);

		// GLFW key code for a key name ("G", "5", "F1", "SPACE", "GRAVE", ...), or -1 if
		// unknown. Single letters/digits map directly; named keys come from the table.
		static int keyFromName(std::string name) {
			for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			if (name.size() == 1) {
				const char c = name[0];
				if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
				if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
			}
			const auto& t = table();
			const auto it = t.find(name);
			return it == t.end() ? -1 : it->second;
		}

		// Canonical display name for a key code (for listing/saving binds). Empty if unknown.
		static std::string nameFromKey(int key) {
			if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return std::string(1, static_cast<char>('A' + (key - GLFW_KEY_A)));
			if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return std::string(1, static_cast<char>('0' + (key - GLFW_KEY_0)));
			for (const auto& kv : table()) if (kv.second == key) return kv.first;
			return "";
		}

		// bind <key> <command line>. Replaces any existing bind on that key.
		// Returns false if the key name is unknown.
		bool bind(const std::string& keyName, const std::string& command) {
			const int k = keyFromName(keyName);
			if (k < 0) return false;
			binds_[k] = command;
			save();
			return true;
		}

		bool unbind(const std::string& keyName) {
			const int k = keyFromName(keyName);
			if (k < 0) return false;
			binds_.erase(k);
			prevDown_.erase(k);
			save();
			return true;
		}

		void unbindAll() { binds_.clear(); prevDown_.clear(); save(); }

		// ---- Persistence (Source-style exec'd config) --------------------------
		// Where binds are saved/loaded. Set once at startup; binds auto-save on every
		// change so they survive across runs.
		void setConfigPath(std::string path) { configPath_ = std::move(path); }

		// Rewrite the config from the current bind table (one `bind KEY COMMAND` line each).
		// No-op if no path is set or while loading (so replaying the file doesn't rewrite it).
		void save() const {
			if (configPath_.empty() || loading_) return;
			std::ofstream os(configPath_, std::ios::trunc);
			if (!os) return;
			os << "// BagelEngine keybinds - auto-generated. Edit via the `bind` console command.\n";
			for (const auto& kv : binds_)
				os << "bind " << nameFromKey(kv.first) << ' ' << kv.second << '\n';
		}

		// Replay the config file through `exec` (typically ConsoleApp::Run), so it behaves
		// like a Source-style exec'd config — every `bind ...` line re-applies. Lines starting
		// with `//` and blank lines are skipped. Auto-save is suppressed during replay.
		void load(ExecFn exec) {
			if (configPath_.empty()) return;
			std::ifstream is(configPath_);
			if (!is) return;
			loading_ = true;
			std::string line;
			while (std::getline(is, line)) {
				const size_t a = line.find_first_not_of(" \t\r\n");
				if (a == std::string::npos) continue;
				const size_t b = line.find_last_not_of(" \t\r\n");
				line = line.substr(a, b - a + 1);
				if (line.empty() || line.compare(0, 2, "//") == 0) continue;
				exec(line.c_str());
			}
			loading_ = false;
		}

		// Per-frame dispatch: fire each bound command on its key's RISING edge (press).
		// `blockInput` (e.g. ImGui::GetIO().WantTextInput) suppresses firing so typing in a
		// text field doesn't trigger binds; edge state still tracks so a held key won't fire
		// the instant focus is released.
		void poll(GLFWwindow* window, bool blockInput, ExecFn exec) {
			for (const auto& kv : binds_) {
				const bool down = glfwGetKey(window, kv.first) == GLFW_PRESS;
				bool& prev = prevDown_[kv.first];
				if (down && !prev && !blockInput) exec(kv.second.c_str());
				prev = down;
			}
		}

		// (keyName, command) for every active bind — for a `bind` listing / future config save.
		std::vector<std::pair<std::string, std::string>> list() const {
			std::vector<std::pair<std::string, std::string>> out;
			out.reserve(binds_.size());
			for (const auto& kv : binds_) out.push_back({ nameFromKey(kv.first), kv.second });
			return out;
		}

		size_t count() const { return binds_.size(); }

	private:
		// Named (non-alphanumeric) keys. Letters A-Z and digits 0-9 are handled directly.
		static const std::unordered_map<std::string, int>& table() {
			static const std::unordered_map<std::string, int> t = {
				{ "SPACE", GLFW_KEY_SPACE },
				{ "GRAVE", GLFW_KEY_GRAVE_ACCENT }, { "TILDE", GLFW_KEY_GRAVE_ACCENT },
				{ "ENTER", GLFW_KEY_ENTER }, { "RETURN", GLFW_KEY_ENTER },
				{ "ESCAPE", GLFW_KEY_ESCAPE }, { "ESC", GLFW_KEY_ESCAPE },
				{ "TAB", GLFW_KEY_TAB }, { "BACKSPACE", GLFW_KEY_BACKSPACE },
				{ "DELETE", GLFW_KEY_DELETE }, { "INSERT", GLFW_KEY_INSERT },
				{ "HOME", GLFW_KEY_HOME }, { "END", GLFW_KEY_END },
				{ "PAGEUP", GLFW_KEY_PAGE_UP }, { "PAGEDOWN", GLFW_KEY_PAGE_DOWN },
				{ "LEFT", GLFW_KEY_LEFT }, { "RIGHT", GLFW_KEY_RIGHT },
				{ "UP", GLFW_KEY_UP }, { "DOWN", GLFW_KEY_DOWN },
				{ "LSHIFT", GLFW_KEY_LEFT_SHIFT }, { "RSHIFT", GLFW_KEY_RIGHT_SHIFT },
				{ "LCTRL", GLFW_KEY_LEFT_CONTROL }, { "RCTRL", GLFW_KEY_RIGHT_CONTROL },
				{ "LALT", GLFW_KEY_LEFT_ALT }, { "RALT", GLFW_KEY_RIGHT_ALT },
				{ "CAPSLOCK", GLFW_KEY_CAPS_LOCK },
				{ "MINUS", GLFW_KEY_MINUS }, { "EQUAL", GLFW_KEY_EQUAL },
				{ "COMMA", GLFW_KEY_COMMA }, { "PERIOD", GLFW_KEY_PERIOD },
				{ "SLASH", GLFW_KEY_SLASH }, { "BACKSLASH", GLFW_KEY_BACKSLASH },
				{ "SEMICOLON", GLFW_KEY_SEMICOLON }, { "APOSTROPHE", GLFW_KEY_APOSTROPHE },
				{ "LBRACKET", GLFW_KEY_LEFT_BRACKET }, { "RBRACKET", GLFW_KEY_RIGHT_BRACKET },
				{ "F1", GLFW_KEY_F1 }, { "F2", GLFW_KEY_F2 }, { "F3", GLFW_KEY_F3 },
				{ "F4", GLFW_KEY_F4 }, { "F5", GLFW_KEY_F5 }, { "F6", GLFW_KEY_F6 },
				{ "F7", GLFW_KEY_F7 }, { "F8", GLFW_KEY_F8 }, { "F9", GLFW_KEY_F9 },
				{ "F10", GLFW_KEY_F10 }, { "F11", GLFW_KEY_F11 }, { "F12", GLFW_KEY_F12 },
			};
			return t;
		}

		std::unordered_map<int, std::string> binds_;     // GLFW key -> command line
		std::unordered_map<int, bool>        prevDown_;   // edge-detect state
		std::string                          configPath_; // persisted-binds file (empty = no persistence)
		bool                                 loading_ = false; // suppress save() while replaying the file
	};

} // namespace bagel
