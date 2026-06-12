#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

namespace bagel {
	//All physics related calculations are done inside jolt system. This component is a container for the pointeres to the bodies.
	struct JoltPhysicsComponent {
		JPH::BodyID bodyID; //transient. should not be serialized
		JPH::BodyCreationSettings settings; // for recreation of the body
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
