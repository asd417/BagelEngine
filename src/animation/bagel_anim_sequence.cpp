#include "animation/bagel_anim_sequence.hpp"

#include "ecs/components/name.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstddef>
#include <sstream>

namespace bagel {

	namespace {

		AnimEasing parseEasing(const std::string &s) {
			if (s == "smooth") return AnimEasing::Smooth;
			if (s == "in") return AnimEasing::In;
			if (s == "out") return AnimEasing::Out;
			return AnimEasing::Linear;
		}

		float ease(AnimEasing e, float u) {
			switch (e) {
			case AnimEasing::Smooth: return u * u * (3.0f - 2.0f * u); // smoothstep
			case AnimEasing::In: return u * u;
			case AnimEasing::Out: return 1.0f - (1.0f - u) * (1.0f - u);
			default: return u;
			}
		}

		// A track value is either a scalar (float properties) or a 3/4-element list. Parsed into
		// the same packed vec4 the property thunks expect. `arity` is the property's own width,
		// so authoring [x,y] for a vec3 is rejected rather than silently zero-filled.
		bool parseValue(const YAML::Node &n, int arity, glm::vec4 &out, std::string &err) {
			out = glm::vec4{0.0f};
			if (!n) {
				err = "missing value";
				return false;
			}
			if (n.IsScalar()) {
				if (arity != 1) {
					err = "expected " + std::to_string(arity) + " numbers, got a single value";
					return false;
				}
				out.x = n.as<float>();
				return true;
			}
			if (!n.IsSequence() || static_cast<int>(n.size()) != arity) {
				err = "expected " + std::to_string(arity) + " numbers";
				return false;
			}
			for (int i = 0; i < arity; ++i)
				out[i] = n[i].as<float>();
			return true;
		}

	} // namespace

	float AnimSequence::length() const {
		float len = 0.0f;
		for (const AnimTrack &t : tracks)
			len = t.end() > len ? t.end() : len;
		return len;
	}

	entt::entity AnimSequencePlayer::findByName(const std::string &name) const {
		auto view = registry.view<const NameComponent>();
		for (auto e : view)
			if (view.get<const NameComponent>(e).is(name.c_str()))
				return e;
		return entt::null;
	}

	bool AnimSequencePlayer::play(const std::string &path, const std::string &name, std::string &outMsg) {
		YAML::Node root;
		try {
			root = YAML::LoadFile(path);
		} catch (const std::exception &e) {
			outMsg = std::string("parse failed: ") + e.what();
			return false;
		}

		const YAML::Node tracks = root["tracks"];
		if (!tracks || !tracks.IsSequence()) {
			outMsg = "no 'tracks' list in " + path;
			return false;
		}

		Instance inst;
		inst.seq.name = name;
		std::ostringstream warn;
		int skipped = 0;

		for (const YAML::Node &tn : tracks) {
			// One bad track is skipped and reported; the rest of the sequence still runs.
			try {
				AnimTrack t;
				t.targetName = tn["target"] ? tn["target"].Scalar() : std::string{};
				const std::string propKey = tn["property"] ? tn["property"].Scalar() : std::string{};
				t.prop = findAnimatable(propKey);
				if (t.targetName.empty() || !t.prop) {
					warn << "\n  skipped track: "
					     << (t.targetName.empty() ? "no target" : "unknown property '" + propKey + "'");
					++skipped;
					continue;
				}

				const entt::entity target = findByName(t.targetName);
				if (target == entt::null) {
					warn << "\n  skipped track: no entity named '" << t.targetName << "'";
					++skipped;
					continue;
				}

				const YAML::Node fromN = tn["from"];
				std::string err;
				if (fromN && fromN.IsScalar() && fromN.Scalar() == "current") {
					t.fromCurrent = true;
				} else if (!parseValue(fromN, t.prop->arity(), t.from, err)) {
					warn << "\n  skipped track on '" << t.targetName << "': from: " << err;
					++skipped;
					continue;
				}
				if (!parseValue(tn["to"], t.prop->arity(), t.to, err)) {
					warn << "\n  skipped track on '" << t.targetName << "': to: " << err;
					++skipped;
					continue;
				}

				t.duration = tn["duration"] ? tn["duration"].as<float>() : 1.0f;
				if (t.duration <= 0.0f)
					t.duration = 0.0001f; // a 0-duration track would divide by zero; snap instead
				t.delay = tn["delay"] ? tn["delay"].as<float>() : 0.0f;
				if (t.delay < 0.0f)
					t.delay = 0.0f;
				t.easing = parseEasing(tn["easing"] ? tn["easing"].Scalar() : std::string{});

				// Resolve `from: current` NOW, at play time — not per frame, or the tween would
				// chase the value it is writing and never converge.
				glm::vec4 start = t.from;
				if (t.fromCurrent && !t.prop->read(registry, target, start)) {
					warn << "\n  skipped track: '" << t.targetName << "' has no "
					     << t.prop->component;
					++skipped;
					continue;
				}

				inst.seq.tracks.push_back(t);
				inst.targets.push_back(target);
				inst.starts.push_back(start);
			} catch (const std::exception &ex) {
				warn << "\n  skipped malformed track: " << ex.what();
				++skipped;
			}
		}

		if (inst.seq.tracks.empty()) {
			outMsg = "no usable tracks in " + path + warn.str();
			return false;
		}

		stop(name); // restart rather than stack a second copy on top of itself
		const float len = inst.seq.length();
		const size_t n = inst.seq.tracks.size();
		active.push_back(std::move(inst));

		std::ostringstream msg;
		msg << "playing '" << name << "': " << n << " track(s), " << len << "s";
		if (skipped > 0)
			msg << " (" << skipped << " skipped)" << warn.str();
		outMsg = msg.str();
		return true;
	}

	void AnimSequencePlayer::stop(const std::string &name) {
		active.erase(std::remove_if(active.begin(), active.end(),
		                            [&](const Instance &i) { return i.seq.name == name; }),
		             active.end());
	}

	void AnimSequencePlayer::stopAll() { active.clear(); }

	bool AnimSequencePlayer::isRunning(const std::string &name) const {
		for (const Instance &i : active)
			if (i.seq.name == name)
				return true;
		return false;
	}

	std::vector<std::string> AnimSequencePlayer::running() const {
		std::vector<std::string> out;
		out.reserve(active.size());
		for (const Instance &i : active)
			out.push_back(i.seq.name);
		return out;
	}

	void AnimSequencePlayer::update(float dt) {
		for (size_t ai = 0; ai < active.size();) {
			Instance &inst = active[ai];
			inst.t += dt;

			for (size_t ti = 0; ti < inst.seq.tracks.size(); ++ti) {
				const AnimTrack &tr = inst.seq.tracks[ti];
				const entt::entity e = inst.targets[ti];
				// A scene switch clears the registry out from under a running sequence.
				if (!registry.valid(e))
					continue;

				const float local = inst.t - tr.delay;
				if (local < 0.0f)
					continue; // not started yet — leave the value alone
				const float u = ease(tr.easing, glm::clamp(local / tr.duration, 0.0f, 1.0f));
				tr.prop->write(registry, e, glm::mix(inst.starts[ti], tr.to, u));
			}

			if (inst.t >= inst.seq.length())
				active.erase(active.begin() + static_cast<ptrdiff_t>(ai));
			else
				++ai;
		}
	}

} // namespace bagel
