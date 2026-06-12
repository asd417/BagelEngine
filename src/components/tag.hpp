#pragma once

// Tag / marker components: trivial structs used to flag entities. They carry no
// (or near-no) data and exist purely so systems can filter the registry by their
// presence.

namespace bagel {
	struct InfoComponent {
		bool a;
	};

	// Marker tag for runtime-only entities (user-controlled camera, editor gizmos,
	// debug/temporary spawns). Entities carrying this are EXCLUDED from maps: their
	// component data is not written, and on load they don't exist. Emplace it when
	// you create an entity that must never be persisted. The tag itself is never
	// serialized. See bagel_ecs_serialize.hpp / map/bagel_map_io.hpp.
	struct Transient {};
}
