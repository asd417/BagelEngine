#pragma once
// Single source of truth for the LEGO connector enums, shared by the parser
// (ldraw_library, which classifies each individual connector) and the connection graph
// (connections.hpp, which pairs connectors into joints). Kept dependency-free (no glm / entt)
// so both the engine-agnostic parser/baker and the engine-side graph can include it.
//
// The bake serializes ConnectorType/ConnFamily by NAME (see baked_connectors), so their integer
// order is not a wire format -- but keep them append-only anyway to avoid surprises.

namespace bagel::ldraw {

	// What kind of LEGO connector an individual primitive represents. These name the connector
	// itself; PinHole/AxleHole are the female HOLES (the male pin/axle that goes through them is
	// a whole part, not a primitive, so it is not detected -- see CONNECTORS.md).
	//   Male     = raised stud on top.
	//   Female   = underside tube/receptacle a stud mates into.
	//   PinHole  = Technic round pin/connector hole (beam hole, connector hole).
	//   AxleHole = Technic cross-shaped axle hole.
	//   Ball     = ball/towball (male half of a ball-and-socket).
	//   Socket   = ball socket (female half).
	enum class ConnectorType { Male, Female, PinHole, AxleHole, Ball, Socket };

	// Which mechanical family a connector belongs to -- disambiguates connectors that share a
	// ConnectorType but can't mate across families, and selects the joint model.
	//   None         = the classic stud/tube/axle/pin connectors.
	//   Joint8       = small Technic "Joint-8" towball/socket (primitive-marked).
	//   Constraction = large sphere-modeled ball (Technic 32474 / Bionicle / Hero Factory).
	//   ClickHinge   = detented "Click Lock Hinge" (clh*) -- poseable, holds at N detents.
	//   Duplo        = Duplo hinge family.
	enum class ConnFamily { None, Joint8, Constraction, ClickHinge, Duplo };

	// The mating type of a connection (which two connectors met). Produced by the detector
	// from the two connectors' ConnectorType+ConnFamily; drives ConnectionsGraph::resolveJoint.
	// See src/lego/CONNECTORS.md.
	enum MateType {
		MALE_FEMALE,     // stud in tube             -- can rotate
		PIN_FEMALE,      // can rotate
		PIN_HOLE,        // can rotate
		AXLE_HOLE,       // round axle in round hole -- can rotate
		HANDLE_AXLESLOT, // can rotate
		AXLE_AXLESLOT,   // cross axle in cross hole -- cannot rotate (keyed)
		BALL_SOCKET,     // ball in socket           -- 3-DOF, motion left to the physics engine
		CLICK_HINGE,     // click-lock hinge         -- 1-DOF hinge that holds pose at detents
	};

	// How a whole joint (all connections shared between a pair of parts) behaves. The physics
	// layer maps this onto a Jolt constraint; the editor uses it for snapping.
	enum class JointKind {
		None,       // not connected
		Hinge,      // 1-DOF free rotation about a shared axis
		ClickHinge, // 1-DOF hinge that holds pose at discrete detents
		Ball,       // 3-DOF ball-and-socket
		Rigid,      // no relative motion (welded)
	};

} // namespace bagel::ldraw
