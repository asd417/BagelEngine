#pragma once

// Source-style `targetname`: a stable, human-authored handle for an entity so things outside
// the C++ code can refer to it. Text maps set it (`- { type: NameComponent, name: $name }`),
// and scripted animation sequences (/anims/*.yaml) use it to find what they drive.
//
// Fixed char array rather than std::string, matching the other authored components
// (TransformHierachyComponent::attachment): trivially copyable and deterministic when
// serialized byte-for-byte.

#include <cstddef>
#include <cstring>

namespace bagel {

	// std::size_t, not int: it is compared against a size_t index in set() below, and a signed
	// bound there would trip -Wsign-compare.
	inline constexpr std::size_t MAX_ENTITY_NAME = 64;

	struct NameComponent {
		char name[MAX_ENTITY_NAME] = {}; // zero-init: valid empty C-string

		NameComponent() = default;
		explicit NameComponent(const char *n) { set(n); }

		// Bounded copy, always NUL-terminated, silently truncating at MAX_ENTITY_NAME-1.
		// Hand-rolled rather than strncpy: the MSVC CRT deprecates that one (C4996 /
		// -Wdeprecated-declarations), and this is warning-free everywhere.
		void set(const char *n) {
			std::size_t i = 0;
			if (n)
				for (; i + 1 < MAX_ENTITY_NAME && n[i] != '\0'; ++i)
					name[i] = n[i];
			name[i] = '\0';
		}
		bool is(const char *n) const { return n && std::strcmp(name, n) == 0; }
	};

}
