#include "smaa/smaa_textures.hpp"
#include "bagel_textures.hpp"

#include <vulkan/vulkan.h>

// The precomputed SMAA lookup tables as raw byte arrays (iryoku/smaa, MIT). Included in exactly
// this one translation unit — the arrays have internal linkage, so including the headers anywhere
// else would duplicate ~1.17 MB per TU.
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

namespace bagel {

	SmaaLutHandles loadSmaaLuts(BGLTextureLoader& loader)
	{
		SmaaLutHandles h{};
		// AreaTex: RG8 (AREATEX_PITCH == width*2), coverage areas. SearchTex: R8, edge search.
		// Sampled at LOD 0 in the blend-weight shader, so the loader's auto mips don't matter.
		h.areaTex   = loader.loadTextureFromMemory("SmaaAreaTex",   areaTexBytes,
			AREATEX_WIDTH,   AREATEX_HEIGHT,   VK_FORMAT_R8G8_UNORM);
		h.searchTex = loader.loadTextureFromMemory("SmaaSearchTex", searchTexBytes,
			SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_FORMAT_R8_UNORM);
		return h;
	}

} // namespace bagel
