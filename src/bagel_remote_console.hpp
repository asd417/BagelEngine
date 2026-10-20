#pragma once

// Localhost line protocol into the developer console, so an external tool can drive a running
// engine. A client connects, sends one console command line ending in '\n', and gets back
// everything that command logged; then the connection is closed. tools/bagel_mcp.py wraps
// this as an MCP server so Claude can call it.
//
// Bound to 127.0.0.1 only. Anything that reaches this port can run any console command.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bagel {

	class RemoteConsole {
	public:
		explicit RemoteConsole(uint16_t port);
		~RemoteConsole();
		RemoteConsole(const RemoteConsole &) = delete;
		RemoteConsole &operator=(const RemoteConsole &) = delete;

		// Main thread, once per frame: run each queued line through `run` and send its result
		// back. Commands touch the registry and Vulkan state, so they never run on the socket thread.
		void poll(const std::function<std::string(const char *)> &run);

	private:
		void listenLoop();

		// Socket handles held as uintptr_t so this header stays free of winsock/posix headers.
		std::uintptr_t listenSock;
		std::thread thread;
		std::mutex mutex;
		std::vector<std::pair<std::uintptr_t, std::string>> pending; // (client socket, command line)
	};

}
