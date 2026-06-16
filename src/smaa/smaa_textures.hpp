#pragma once
#include <cstdint>

namespace bagel {

	class BGLTextureLoader;

	// Bindless handles for SMAA's two precomputed lookup tables (used by the blending-weight pass).
	struct SmaaLutHandles {
		uint32_t areaTex   = 0; // RG8 160x560 — coverage areas
		uint32_t searchTex = 0; // R8  64x16   — edge-distance search
	};

	// Upload the precomputed AreaTex (RG8) and SearchTex (R8) from the embedded iryoku tables and
	// return their bindless handles. Call once at startup. Isolated in its own translation unit so
	// the ~1.17 MB byte arrays don't bloat other files' recompiles.
	SmaaLutHandles loadSmaaLuts(BGLTextureLoader& loader);

} // namespace bagel
