#pragma once

#include <string>
#include <stdexcept>

namespace bagel {
namespace util {
	std::string enginePathString(std::string path);
	std::string enginePath(const char* path);

	//refer to https://en.cppreference.com/w/cpp/io/c/fprintf
	template<typename ... Args>
	std::string formatString(const std::string& format, Args ... args) {
		//https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
		int size_s = _snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
		if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
		auto size = static_cast<size_t>(size_s);
		std::unique_ptr<char[]> buf(new char[size]);
		_snprintf(buf.get(), size, format.c_str(), args ...);
		return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
	}

}
}