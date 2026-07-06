#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <glm/glm.hpp>

namespace bagel {
	//All physics related calculations are done inside jolt system. This component is a container for the pointeres to the bodies.
	struct JoltPhysicsComponent {
		JPH::BodyID bodyID; //transient. should not be serialized
		JPH::BodyCreationSettings settings; // for recreation of the body
	};

	// A part fused into a multi-part solid group (see LegoConnectionGraph::binSolidGroups /
	// BGLJolt::BuildBodiesPerGroup). The group's single rigid body lives on the group's reference
	// part as a normal JoltPhysicsComponent; every OTHER member carries this instead and rides
	// that shared body at a fixed offset. BGLJolt::ApplyGroupTransforms() writes each follower's
	// world transform = refBodyWorld * localOffset every frame. All fields are transient.
	struct JoltGroupMemberComponent {
		JPH::BodyID groupBody;            // shared body, owned by the reference part
		glm::mat4   localOffset{ 1.0f };  // this part's transform relative to the body origin
	};

	struct JoltKinematicComponent {
		enum MoveMode {
			//"Moves" to position over frameTime. Exerts force on contact
			PHYSICAL,
			//"Teleports" to position. Does not exert force on contact
			IMMEDIATE,
			//"Teleports" to position. Does not exert force on contact. Only moves if change is larger than some small value
			IMMEDIATE_OPTIMAL
		};

		JPH::BodyID bodyID;
		MoveMode moveMode = MoveMode::PHYSICAL;
		JPH::BodyCreationSettings settings; // for recreation of the body
	};
}
