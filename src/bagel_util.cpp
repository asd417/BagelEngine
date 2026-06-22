#include "bagel_util.hpp"

#include <memory>
#pragma warning(disable : 4996)

namespace bagel {
namespace util {
	std::string enginePath(const char* path) {
		return std::string(ENGINE_BASE_PATH).append(path);
	}

}
}