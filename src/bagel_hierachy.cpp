#include "bagel_hierachy.hpp"

#include <cmath>

namespace bagel {

	// ---- attachment query API ---------------------------------------------------------------

	int lookupAttachment(entt::registry& registry, entt::entity entity, const std::string& name) {
		auto* ac = registry.try_get<AttachmentComponent>(entity);
		return ac ? ac->lookup(name) : -1;
	}

	bool getAttachmentWorld(entt::registry& registry, entt::entity entity, int index, glm::mat4& outWorld) {
		auto* ac = registry.try_get<AttachmentComponent>(entity);
		if (!ac || index < 0 || index >= static_cast<int>(ac->points.size())) return false;
		auto* tc = registry.try_get<TransformComponent>(entity);
		if (!tc) return false;

		const AttachmentComponent::Point& p = ac->points[index];
		glm::mat4 boneGlobal{ 1.0f };
		if (auto* anim = registry.try_get<AnimationComponent>(entity))
			if (p.joint >= 0 && p.joint < static_cast<int>(anim->currentGlobals.size()))
				boneGlobal = anim->currentGlobals[p.joint];

		outWorld = tc->mat4() * boneGlobal * p.localOffset; // entityWorld * boneGlobal * localOffset
		return true;
	}

	// Extract translation / per-axis scale / Euler XYZ from an affine matrix, matching the
	// X1Y2Z3 column layout TransformComponent::mat4() builds (so feeding the Euler back through
	// mat4() reproduces this rotation). Assumes no shear (true for bone + uniform-ish transforms).
	static void decomposeToEuler(const glm::mat4& m, glm::vec3& t, glm::vec3& euler, glm::vec3& scale) {
		t = glm::vec3(m[3]);
		const glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
		scale = { glm::length(c0), glm::length(c1), glm::length(c2) };
		const glm::vec3 r0 = scale.x > 1e-8f ? c0 / scale.x : glm::vec3(1, 0, 0);
		const glm::vec3 r1 = scale.y > 1e-8f ? c1 / scale.y : glm::vec3(0, 1, 0);
		const glm::vec3 r2 = scale.z > 1e-8f ? c2 / scale.z : glm::vec3(0, 0, 1);
		// glm column-major: r0=column0, etc. Convention element M[row][col] = column[col][row].
		const float s2 = glm::clamp(r2.x, -1.0f, 1.0f); // M[0][2] = s2
		euler.y = std::asin(s2);
		euler.x = std::atan2(-r2.y, r2.z);              // M[1][2]=-c2s1, M[2][2]=c1c2
		euler.z = std::atan2(-r1.x, r0.x);              // M[0][1]=-c2s3, M[0][0]=c2c3
	}

	HierachySystem::HierachySystem(entt::registry& _r) : registry{_r}
	{}

	void HierachySystem::CreateHierachy(entt::entity parent, entt::entity child, const std::string& attachment) {
		TransformHierachyComponent* p = registry.try_get<TransformHierachyComponent>(parent);
		if (registry.all_of<JoltPhysicsComponent>(child))
		{
			// Dynamic bodies are owned by Jolt in world space; parenting would fight the solver.
			//CONSOLE->Log("Hierachy", "Refused: dynamic physics body cannot be a hierarchy child");
			return;
		}
		if (p == nullptr) {
			p = &registry.emplace<bagel::TransformHierachyComponent>(parent);
		}

		TransformHierachyComponent* c = registry.try_get<TransformHierachyComponent>(child);
		if (c == nullptr) {
			c = &registry.emplace<bagel::TransformHierachyComponent>(child);
		}

		c->hasParent = true;
		c->parent = parent;
		c->depth = p->depth + 1;
		c->attachment = attachment;
	}

	void HierachySystem::ResolveSkeletonGlobals() {
		for (auto [e, anim] : registry.view<AnimationComponent>().each()) {
			if (anim.skeleton.empty() || anim.jointCount == 0) { anim.currentGlobals.clear(); continue; }
			// editPose + IK = the authored/posed bones (same final pose the palette bakes for
			// manualPose). Resolved here so attachment points are current before parenting.
			Pose pose;
			applyManualPose(anim.skeleton, anim.editPose, anim.ikSetups, pose);
			resolveGlobals(anim.skeleton, pose, anim.currentGlobals);
		}
	}

	void HierachySystem::ApplyHiarchialChange()
	{
		registry.sort<TransformHierachyComponent>(
			[](const TransformHierachyComponent& lhs, const TransformHierachyComponent& rhs) {
				return lhs.depth < rhs.depth;
			});

		auto view = registry.view<TransformHierachyComponent, TransformComponent>();
		view.use<TransformHierachyComponent>();

		for (auto [entity, hier, tc] : view.each()) {
			if (!hier.hasParent) continue;

			// Attachment parenting: ride a named attach point on the parent (bone-anchored) instead
			// of the parent's root transform. Falls through to root parenting if the point is missing.
			if (!hier.attachment.empty()) {
				glm::mat4 aw;
				const int ai = lookupAttachment(registry, hier.parent, hier.attachment);
				if (ai >= 0 && getAttachmentWorld(registry, hier.parent, ai, aw)) {
					glm::vec3 at, ar, as;
					decomposeToEuler(aw, at, ar, as);
					const float c3 = cosf(ar.z), s3 = sinf(ar.z);
					const float c2 = cosf(ar.y), s2 = sinf(ar.y);
					const float c1 = cosf(ar.x), s1 = sinf(ar.x);
					const glm::mat3 attRot{
						{ c2*c3,           c1*s3 + c3*s1*s2,  s1*s3 - c1*c3*s2 },
						{-c2*s3,           c1*c3 - s1*s2*s3,  c3*s1 + c1*s2*s3 },
						{ s2,             -c2*s1,              c1*c2            }
					};
					tc.setTranslation(at + attRot * (as * hier.localTranslation));
					tc.setRotation(ar + hier.localRotation);
					tc.setScale(as * hier.localScale);
					continue;
				}
				// else: attachment unresolved this frame — fall back to root parenting below.
			}

			auto& ptc = registry.get<TransformComponent>(hier.parent);

			// Pure rotation matrix from parent's world rotation (XYZ Euler, no scale/translation)
			const glm::vec3 pr = ptc.getRotation();
			const float c3 = cosf(pr.z), s3 = sinf(pr.z);
			const float c2 = cosf(pr.y), s2 = sinf(pr.y);
			const float c1 = cosf(pr.x), s1 = sinf(pr.x);
			const glm::mat3 parentRot{
				{ c2*c3,           c1*s3 + c3*s1*s2,  s1*s3 - c1*c3*s2 },
				{-c2*s3,           c1*c3 - s1*s2*s3,  c3*s1 + c1*s2*s3 },
				{ s2,             -c2*s1,              c1*c2            }
			};

			const glm::vec3 ps = ptc.getScale();
			tc.setTranslation(ptc.getTranslation() + parentRot * (ps * hier.localTranslation));
			tc.setRotation(pr + hier.localRotation);
			tc.setScale(ps * hier.localScale);
		}
	}
}
