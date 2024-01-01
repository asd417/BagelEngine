#include "bagel_util.hpp"

#define ENGINEPATH "C:/Users/locti/OneDrive/Documents/VisualStudioProjects/VulkanEngine"

namespace bagel {
namespace util {
	std::string enginePathString(std::string path) {
		return (ENGINEPATH + path);
	}

	std::string enginePath(const char* path) {
		return std::string(ENGINEPATH).append(path);
	}
}
}