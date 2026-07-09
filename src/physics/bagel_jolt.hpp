#pragma once
#include <iostream>
#include <cstdarg>
#include <thread>

// Jolt includes
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <Jolt/Renderer/DebugRenderer.h>

#include "entt.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../bagel_engine_device.hpp"
#include "../bagel_model.hpp"
#include "math/bagel_math.hpp"

// All Jolt symbols are in the JPH namespace
// If you want your code to compile using single or double precision write 0.0_r to get a Real value that compiles to double or float depending if JPH_DOUBLE_PRECISION is set or not.
//using namespace JPH::literals;



namespace bagel {

	// Layer that objects can be in, determines which other objects it can collide with
	// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
	// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
	// but only if you do collision testing).
	namespace PhysicsLayers
	{
		static constexpr JPH::ObjectLayer NON_MOVING = 0;
		static constexpr JPH::ObjectLayer MOVING = 1;
		// Moving bodies that collide with the static world (ground) ONLY — never with each other or
		// other moving bodies. For assemblies driven by constraints/grouping rather than contacts
		// (e.g. LEGO parts), mutual collision is pure cost + jitter. Shares the MOVING broadphase tree.
		static constexpr JPH::ObjectLayer GROUND_ONLY = 2;
		static constexpr JPH::ObjectLayer NUM_LAYERS = 3;
	};

	enum PhysicsType
	{
		STATIC,
		KINEMATIC,
		DYNAMIC
	};

	// Callback for traces, connect this to your own trace function if you have one
	static void TraceImpl(const char* inFMT, ...)
	{
		// Format the message
		va_list list;
		va_start(list, inFMT);
		char buffer[1024];
		vsnprintf(buffer, sizeof(buffer), inFMT, list);
		va_end(list);

		// Print to the TTY
		std::cout << buffer << std::endl;
	}

	// Callback for asserts, connect this to your own assert handler if you have one
	static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
	{
		// Print to the TTY
		std::cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "") << std::endl;

		// Breakpoint
		return true;
	};

	// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
	// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
	// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
	// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
	// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
	namespace BroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer MOVING(1);
		static constexpr JPH::uint NUM_LAYERS(2);
	};


	// An example contact listener
	class MyContactListener : public JPH::ContactListener
	{
	public:
		// See: ContactListener
		virtual JPH::ValidateResult	OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
		{
			//std::cout << "Contact validate callback" << std::endl;

			// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;

		virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
		{
			//std::cout << "A contact was persisted" << std::endl;
		}

		virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
		{
			//std::cout << "A contact was removed" << std::endl;
		}
	};

	// An example activation listener
	class MyBodyActivationListener : public JPH::BodyActivationListener
	{
	public:
		virtual void OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override;

		virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override;
	};

	/// Class that determines if an object layer can collide with a broadphase layer
	class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
		{
			switch (inLayer1)
			{
			case PhysicsLayers::NON_MOVING:
				return inLayer2 == BroadPhaseLayers::MOVING;
			case PhysicsLayers::MOVING:
				return true;
			case PhysicsLayers::GROUND_ONLY:
				return inLayer2 == BroadPhaseLayers::NON_MOVING; // only tests against the static world
			default:
				JPH_ASSERT(false);
				return false;
			}
		}
	};


	// BroadPhaseLayerInterface implementation
	// This defines a mapping between object and broadphase layers.
	class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		BPLayerInterfaceImpl()
		{
			// Create a mapping table from object to broad phase layer
			mObjectToBroadPhase[PhysicsLayers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
			mObjectToBroadPhase[PhysicsLayers::MOVING] = BroadPhaseLayers::MOVING;
			mObjectToBroadPhase[PhysicsLayers::GROUND_ONLY] = BroadPhaseLayers::MOVING; // these bodies move
		}

		virtual JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM_LAYERS;
		}

		virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
		{
			JPH_ASSERT(inLayer < PhysicsLayers::NUM_LAYERS);
			return mObjectToBroadPhase[inLayer];
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
		{
			switch ((JPH::BroadPhaseLayer::Type)inLayer)
			{
			case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
			case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
			default:													JPH_ASSERT(false); return "INVALID";
			}
		}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

	private:
		JPH::BroadPhaseLayer mObjectToBroadPhase[PhysicsLayers::NUM_LAYERS];
	};

	/// Class that determines if two object layers can collide
	class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
		{
			// GROUND_ONLY bodies collide with the static world only — never with each other and
			// never with other moving bodies. Handled first so the rule stays symmetric regardless
			// of argument order.
			if (inObject1 == PhysicsLayers::GROUND_ONLY || inObject2 == PhysicsLayers::GROUND_ONLY)
			{
				JPH::ObjectLayer other = (inObject1 == PhysicsLayers::GROUND_ONLY) ? inObject2 : inObject1;
				return other == PhysicsLayers::NON_MOVING;
			}
			switch (inObject1)
			{
			case PhysicsLayers::NON_MOVING:
				return inObject2 == PhysicsLayers::MOVING; // Non moving only collides with moving
			case PhysicsLayers::MOVING:
				return true; // Moving collides with everything (LEGO already handled above)
			default:
				JPH_ASSERT(false);
				return false;
			}
		}
	};


	class BGLJolt {
	public:

		struct PhysicsBodyCreationInfo {
			glm::vec3 pos;
			glm::vec3 rot;
			PhysicsType physicsType;
			bool activate;
			JPH::ObjectLayer layer;
		};

		static void Initialize(BGLDevice& device, entt::registry& registry) {
			// Register allocation hook
			JPH::RegisterDefaultAllocator();
			// Install callbacks
			JPH::Trace = TraceImpl;
			//JPH::JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

			// Create a factory
			JPH::Factory::sInstance = new JPH::Factory();

			// Register all Jolt physics types
			JPH::RegisterTypes();

			instance = new BGLJolt(device,registry);
		}
		static BGLJolt* GetInstance() { 
			if (instance == nullptr) throw std::runtime_error("BGLJolt must be initialized with entt registry reference");
			return instance;
		}
		JPH::BodyInterface& GetBodyInterface() const { return *bodyInterface; }
		void Step(float dt, JPH::uint stepCount = 1);
		void SetComponentActivityAll(bool activity);
		void SetComponentActivity(entt::entity ent, bool activity);
		// True if the body is currently awake (used by the inspector's sleep readout).
		bool IsBodyActive(entt::entity ent);
		// Remove + destroy the live Jolt body(ies) for one entity (physics and/or kinematic), if
		// any. Call BEFORE registry.destroy(ent) so the body doesn't leak in the physics system.
		void RemoveEntityBody(entt::entity ent);
		void ApplyTransformToKinematic(float dt);
		void ApplyPhysicsTransform();
		// Recreate live Jolt bodies for every entity carrying a Jolt component from its
		// serialized BodyCreationSettings, assigning fresh (transient) BodyIDs. Call after
		// a map load — the loaded bodyIDs are meaningless until the engine re-issues them.
		void RehydratePhysicsBodies();
		// Remove + destroy every live Jolt body referenced by a Jolt component. Call BEFORE
		// clearing/reloading the registry so the bodies don't leak inside the physics system
		// (the components have no destructor that touches Jolt).
		void RemoveAllBodies();
		void AddSphere(entt::entity ent, float radius, PhysicsBodyCreationInfo &info);
		void AddBox(entt::entity ent, glm::vec3 halfExtent, PhysicsBodyCreationInfo& info);
		// Convex-hull collider baked offline (lego/baked/collision/<part>.glb). Each inner
		// vector is one hull's point cloud in body-local space; one hull -> ConvexHullShape,
		// many -> a StaticCompoundShape of hulls. Points must already be in the entity's
		// local/model scale (the caller scales the raw-LDU bake by the load scale).
		void AddConvexHull(entt::entity ent, const std::vector<std::vector<glm::vec3>>& hulls,
		                   PhysicsBodyCreationInfo& info);
		// Fuse each solid group into ONE body: the members' baked collider shapes (ColliderShapeComponent)
		// are combined into a compound placed at each part's pose relative to the group's reference
		// (first) part, tagged with the member entity as sub-shape user data (for raycast picking), and
		// the followers get a JoltGroupMemberComponent so ApplyGroupTransforms() can ride them along.
		// Groups of size < 2 are ignored (singletons keep their own body). `groups` is any entity
		// partition — e.g. LegoConnectionGraph::binSolidGroups() — so this stays domain-agnostic.
		void BuildBodiesPerGroup(const std::vector<std::vector<entt::entity>>& groups,
		                         PhysicsType type = PhysicsType::DYNAMIC,
		                         JPH::ObjectLayer layer = PhysicsLayers::MOVING);
		// Rebuild ONE solid group's body from its current member list — call after an edit adds or
		// removes a part (bricks are placed/removed at human tempo, so rebuilding a StaticCompound is
		// cheap; MutableCompoundShape is unnecessary). Tears down the group's existing body/components
		// first. A size-1 group collapses back to a single per-part body from its ColliderShapeComponent.
		void RebuildGroupBody(const std::vector<entt::entity>& group,
		                      PhysicsType type = PhysicsType::DYNAMIC,
		                      JPH::ObjectLayer layer = PhysicsLayers::MOVING);
		// Drive every JoltGroupMemberComponent follower from its shared group body. Call each frame
		// right after ApplyPhysicsTransform() (which drives the reference parts that own the bodies).
		void ApplyGroupTransforms();
		// Map a raycast hit (body + sub-shape) back to the block entity it struck: reads the entity
		// tag from the group compound's sub-shape user data, or the body user data for a lone part.
		// Returns entt::null if it can't be resolved.
		entt::entity resolveRaycastToBlock(JPH::BodyID body, JPH::SubShapeID subShape) const;
		// Convenience: cast a world-space ray into the physics scene and return the block entity under
		// it (entt::null on miss). `dir` need not be normalized; it is scaled by `maxDist`. If `outHit`
		// is non-null it receives the world hit point.
		entt::entity PickEntity(glm::vec3 origin, glm::vec3 dir, float maxDist = 1000.0f,
		                        glm::vec3* outHit = nullptr) const;
		entt::entity PickEntity(Ray ray, glm::vec3 *outHit = nullptr) const;
		void SetSimulationTimescale(float _s) { simTimeScale = _s; }
		glm::vec3 GetGravity();
		void SetGravity(glm::vec3 gravity);
	private:
		// Build one group's compound body (assumes the group's old bodies/components are already torn
		// down). Returns the new body id, or an invalid id if fewer than 2 members had shapes.
		JPH::BodyID buildGroupCompound(const std::vector<entt::entity>& group, PhysicsType type, JPH::ObjectLayer layer);
		BGLJolt(BGLDevice& _bglDevice, entt::registry& _registry);
		~BGLJolt();

		//Singleton Pattern
		static BGLJolt* instance;
		entt::registry& registry;
		BGLDevice& bglDevice;

		// We need a job system that will execute physics jobs on multiple threads. Typically
		// you would implement the JobSystem interface yourself and let Jolt Physics run on top
		// of your own job scheduler. JobSystemThreadPool is an example implementation.
		JPH::JobSystemThreadPool job_system = JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

		// We need a temp allocator for temporary allocations during the physics update.We're
		// pre-allocating 10 MB to avoid having to do allocations during the physics update.
		// B.t.w. 10 MB is way too much for this example but it is a typical value you can use.
		// If you don't want to pre-allocate you can also use TempAllocatorMalloc to fall back to
		// malloc / free.
		JPH::TempAllocatorImpl tempAllocator = JPH::TempAllocatorImpl(10 * 1024 * 1024);

		// This is the max amount of rigid bodies that you can add to the physics system. If you try to add more you'll get an error.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
		const JPH::uint cMaxBodies = 1024;

		// This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
		const JPH::uint cNumBodyMutexes = 0;

		// This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
		// body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
		// too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
		const JPH::uint cMaxBodyPairs = 1024;

		// This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
		// number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 10240.
		const JPH::uint cMaxContactConstraints = 1024;

		// Create mapping table from object layer to broadphase layer
		// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
		BPLayerInterfaceImpl broad_phase_layer_interface;

		// Create class that filters object vs broadphase layers
		// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
		ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;

		// Create class that filters object vs object layers
		// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
		ObjectLayerPairFilterImpl object_vs_object_layer_filter;

		JPH::PhysicsSystem physicsSystem;
		JPH::BodyInterface* bodyInterface;

		// A body activation listener gets notified when bodies activate and go to sleep
		// Note that this is called from a job so whatever you do here needs to be thread safe.
		// Registering one is entirely optional.
		MyBodyActivationListener body_activation_listener;

		// A contact listener gets notified when bodies (are about to) collide, and when they separate again.
		// Note that this is called from a job so whatever you do here needs to be thread safe.
		// Registering one is entirely optional.
		MyContactListener contact_listener;

		float simTimeScale = 0.2f;
	};
}