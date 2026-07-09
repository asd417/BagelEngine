#pragma once

#include "engine/bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "engine/bagel_descriptors.hpp"

#include <glm/glm.hpp>
#include <memory>

namespace bagel {

	// Owns the two resident SSBOs for skeletal skinning, both host-visible/mapped and
	// bump-allocated as skinned models load (mirrors BGLMaterialManager's skin table):
	//
	//   SKIN buffer    — per-vertex influences (8 bytes: 4×u8 joint index + 4×u8 unorm weight).
	//                    Read by the skinned vertex shader as v[skinVertexBase + gl_VertexIndex].
	//   PALETTE buffer — baked joint matrices (glm::mat4). Read as m[animBaseOffset + jointIndex].
	//
	// Both are registered once into the bindless descriptor set (bindings SKIN / PALETTE).
	// A row's origin (baked at load vs. written live for IK/generative) is invisible to the GPU.
	class BGLSkinManager {
	public:
		BGLSkinManager(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager);

		// Append `vertexCount` influence entries (8 bytes each). Returns the base vertex index
		// the model stores as ModelComponent::skinVertexBase.
		uint32_t uploadInfluences(const void* data, uint32_t vertexCount);

		// Append `matrixCount` baked palette matrices. Returns the base matrix index the model's
		// AnimationComponent stores as paletteBase.
		uint32_t uploadPalette(const glm::mat4* data, uint32_t matrixCount);

		// Bump-allocate `matrixCount` palette slots WITHOUT writing them, returning the base.
		// Used for manual posing / IK: the caller overwrites the region later with writePalette.
		uint32_t reservePalette(uint32_t matrixCount);

		// Overwrite `count` matrices at an already-reserved `base` (does not advance the cursor).
		// `base + count` must lie within a region previously handed out by reservePalette/uploadPalette.
		void writePalette(uint32_t base, const glm::mat4* data, uint32_t count);

		// Reset both allocators for a new scene (GPU must be idle; old contents get overwritten).
		void clear() { skinCursor = 0; paletteCursor = 0; }

	private:
		static constexpr uint32_t INFLUENCE_STRIDE   = 8;          // bytes per vertex (must match SkinInfluence)
		static constexpr uint32_t MAX_SKIN_VERTICES  = 1u << 20;   // 1,048,576 verts * 8B  = 8 MB
		static constexpr uint32_t MAX_PALETTE_MATRICES = 200000;   // 200k mat4    * 64B = ~12.8 MB

		BGLDevice& device;
		std::unique_ptr<BGLBuffer> skinBuffer;    // binding SKIN
		std::unique_ptr<BGLBuffer> paletteBuffer; // binding PALETTE
		uint32_t skinCursor    = 0;               // next free vertex slot
		uint32_t paletteCursor = 0;               // next free matrix slot
	};

} // namespace bagel
