#pragma once

#include <memory>
#include <cstdint>
#include <cstddef>

#include "engine/bagel_engine_device.hpp"
#include "engine/bagel_descriptors.hpp"

namespace bagel {
	struct DataBufferComponent {
		std::unique_ptr<BGLBuffer> objDataBuffer;
		uint32_t bufferHandle;

		DataBufferComponent(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager, uint32_t bufferUnitsize, const char* bufferName);
		// Move-only: owns a GPU buffer via the unique_ptr. Copying would double-free it;
		// entt relocates by moving. No custom destructor — BGLBuffer's destructor unmaps
		// and frees. (The old destructor only redundantly unmapped and would null-deref a
		// moved-from instance, leaving the component neither copyable nor movable.)
		DataBufferComponent(const DataBufferComponent&) = delete;
		DataBufferComponent& operator=(const DataBufferComponent&) = delete;
		DataBufferComponent(DataBufferComponent&&) noexcept = default;
		DataBufferComponent& operator=(DataBufferComponent&&) noexcept = default;
		void writeToBuffer(void* data, size_t size, size_t offset);
		uint32_t getBufferHandle() const { return bufferHandle; }
	};
}
