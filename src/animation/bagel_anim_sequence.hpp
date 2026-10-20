#pragma once
// -----------------------------------------------------------------------------
// Scripted animation sequences: hand-authored tweens loaded from /anims/<name>.yaml.
//
// This is the "start value, end value, duration" model — NOT a keyframe editor. A sequence is
// a list of tracks; each track drives ONE animatable property (see bagel_animatable.hpp) on
// ONE named entity (NameComponent) from `from` to `to` over `duration` seconds, after an
// optional `delay`. Overlapping/staggered tracks are how a multi-part sequence is expressed.
//
//   # /anims/door.yaml
//   tracks:
//     - target:   big_door                       # NameComponent on the entity
//       property: TransformComponent.position    # "Component.property", see `anim props`
//       from:     [0, 0, 0]                      # or `current` to start from wherever it is
//       to:       [0, -3, 0]
//       duration: 2.0
//       delay:    0.0                            # optional, seconds before this track starts
//       easing:   smooth                         # linear | smooth | in | out (default linear)
//
// Play it from the console with `anim door`. The file is re-read from disk on every play, so
// editing the YAML and re-running the command is the whole authoring loop.
// -----------------------------------------------------------------------------

#include "animation/bagel_animatable.hpp"

#include "entt.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace bagel {

	enum class AnimEasing { Linear, Smooth, In, Out };

	struct AnimTrack {
		std::string targetName;
		const AnimatableProperty *prop = nullptr;
		glm::vec4 from{0.0f};
		glm::vec4 to{0.0f};
		bool fromCurrent = false; // `from: current` — sampled when the sequence starts
		float delay = 0.0f;
		float duration = 1.0f;
		AnimEasing easing = AnimEasing::Linear;
		float end() const { return delay + duration; }
	};

	struct AnimSequence {
		std::string name; // the file's base name
		std::vector<AnimTrack> tracks;
		float length() const; // longest track end, seconds
	};

	class AnimSequencePlayer {
	public:
		explicit AnimSequencePlayer(entt::registry &reg) : registry{reg} {}

		// Parse `path` and start it, replacing any running instance of the same name. Reads the
		// file fresh every call. False + a reason in `outMsg` on a parse/lookup failure; a
		// per-track problem (unknown property, missing target) is reported in outMsg but does
		// not stop the rest of the sequence.
		bool play(const std::string &path, const std::string &name, std::string &outMsg);

		void stop(const std::string &name);
		void stopAll();

		// Advance every running sequence and write the interpolated values. Instances retire
		// when their longest track finishes.
		void update(float dt);

		std::vector<std::string> running() const;
		bool isRunning(const std::string &name) const;

	private:
		struct Instance {
			AnimSequence seq;
			std::vector<entt::entity> targets; // resolved at play time, one per track
			std::vector<glm::vec4> starts;     // `from`, with `current` already sampled
			float t = 0.0f;
		};

		entt::entity findByName(const std::string &name) const;

		entt::registry &registry;
		std::vector<Instance> active;
	};

} // namespace bagel
