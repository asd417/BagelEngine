#include "bagel_remote_console.hpp"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
static const sock_t kBadSock = INVALID_SOCKET;
static void closeSock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using sock_t = int;
static const sock_t kBadSock = -1;
static void closeSock(sock_t s) { close(s); }
#endif

namespace bagel {

	static std::uintptr_t toHandle(sock_t s) { return static_cast<std::uintptr_t>(s); }
	static sock_t fromHandle(std::uintptr_t h) { return static_cast<sock_t>(h); }

	RemoteConsole::RemoteConsole(uint16_t port)
	{
#ifdef _WIN32
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
		sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only, see the header
		if (s == kBadSock ||
			bind(s, reinterpret_cast<const sockaddr *>(&addr), static_cast<socklen_t>(sizeof(addr))) != 0 ||
			listen(s, 4) != 0) {
			std::cerr << "[remote console] cannot listen on 127.0.0.1:" << port << " (another engine running?)\n";
			if (s != kBadSock) closeSock(s);
			listenSock = toHandle(kBadSock);
			return;
		}
		listenSock = toHandle(s);
		std::cout << "[remote console] listening on 127.0.0.1:" << port << "\n";
		thread = std::thread(&RemoteConsole::listenLoop, this);
	}

	RemoteConsole::~RemoteConsole()
	{
		if (thread.joinable()) {
			// Closing the listen socket fails the blocked accept() and ends listenLoop.
			sock_t s = fromHandle(listenSock);
			shutdown(s, 2); // SD_BOTH / SHUT_RDWR; on Linux close() alone doesn't wake accept()
			closeSock(s);
			thread.join();
		}
		for (auto &p : pending)
			closeSock(fromHandle(p.first));
#ifdef _WIN32
		WSACleanup();
#endif
	}

	void RemoteConsole::listenLoop()
	{
		for (;;) {
			sock_t c = accept(fromHandle(listenSock), nullptr, nullptr);
			if (c == kBadSock)
				return; // listen socket closed by the destructor

			// ponytail: requests are read one client at a time on this thread; a client that
			// connects and sends nothing holds up the others until this timeout.
#ifdef _WIN32
			DWORD timeoutMs = 2000;
			setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs), static_cast<socklen_t>(sizeof(timeoutMs)));
#else
			timeval tv{2, 0};
			setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, static_cast<socklen_t>(sizeof(tv)));
#endif
			std::string line;
			char ch;
			while (line.size() < 4096 && recv(c, &ch, 1, 0) == 1 && ch != '\n')
				if (ch != '\r') line += ch;

			if (line.empty()) {
				closeSock(c);
				continue;
			}
			std::lock_guard<std::mutex> lock(mutex);
			pending.emplace_back(toHandle(c), std::move(line));
		}
	}

	void RemoteConsole::poll(const std::function<std::string(const char *)> &run)
	{
		std::vector<std::pair<std::uintptr_t, std::string>> batch;
		{
			std::lock_guard<std::mutex> lock(mutex);
			batch.swap(pending);
		}
		for (auto &[handle, line] : batch) {
			const std::string out = run(line.c_str());
			sock_t c = fromHandle(handle);
			send(c, out.data(), static_cast<int>(out.size()), 0);
			closeSock(c);
		}
	}

}
