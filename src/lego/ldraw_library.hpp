#pragma once
// LDraw parts-library parser (engine-agnostic; depends only on glm + std).
//
// An LDraw part is a line-based text file (.dat) that recursively references
// sub-files (other parts, sub-parts under parts/s/, and primitives under p/).
// Library::bake() resolves that whole tree in a single recursive walk and emits
// BOTH the flattened triangle mesh AND the LEGO connection points (studs and
// underside tubes), which fall out of the same traversal: every reference to a
// stud-family primitive is a connection point at the accumulated transform.
//
// Units: 1 LDU = 0.4 mm. Stud pitch = 20 LDU, brick = 24 LDU tall, plate = 8 LDU.

#include "connector_types.hpp"   // shared ConnectorType / ConnFamily / MateType / JointKind

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel::ldraw {

	// A single connection point in part-local space (LDU). `orient` is the primitive's
	// accumulated 3x3 basis: its +Y column is the stud axis (which way the stud faces).
	struct ConnectionPoint {
		glm::vec3   pos{ 0.0f };
		glm::mat3   orient{ 1.0f };
		ConnectorType    type = ConnectorType::Male;
		ConnFamily  family = ConnFamily::None;   // mechanical family (mating + joint model)
		// Part-local id grouping connectors that share one rotation-axis line (parallel
		// axes lying on a common line). A long axle/pin through collinear holes keeps that
		// group's hinge; offset-parallel connectors (a brick's studs) get separate ids so
		// the joint reads rigid. Assigned by the offline baker; -1 when not baked.
		int         axisGroup = -1;
		int         detents = 0;   // ClickHinge: number of click-stops (from "N-Position"); else 0
		std::string prim;   // primitive that produced it, e.g. "stud.dat"
	};

	// Assign ConnectionPoint::axisGroup over a part's connectors: connectors whose axes are
	// parallel AND lie on a common line get the same group id (0..N-1). Lets the joint resolver
	// tell a collinear axle/pin hinge (one group) from an offset-parallel rigid set (many).
	// Group ids are part-local; idempotent, so it is safe on hand-authored connector lists too.
	void assignAxisGroups(std::vector<ConnectionPoint>& conns);

	// Flattened, transformed geometry. positions/normals are parallel; indices are
	// 3-per-triangle. Not welded (the engine-side loader can weld/index later).
	struct BakedMesh {
		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<uint32_t>  indices;
		size_t triangleCount() const { return indices.size() / 3; }
	};

	struct BakeResult {
		BakedMesh                    mesh;
		std::vector<ConnectionPoint> connections;
		int maleCount() const;
		int femaleCount() const;
		int pinCount() const;
		int axleCount() const;
	};

	class Library {
	public:
		// root = folder that directly contains parts/ and p/  (e.g. ".../lego/ldraw").
		explicit Library(std::string root) : root_(std::move(root)) {}

		// Bake a part by name ("3001", "3001.dat", or a sub-path). Returns an empty
		// result (and logs) if the part cannot be resolved.
		BakeResult bake(const std::string& name);

		// Diagnostics from the most recent bake.
		int unresolvedCount() const { return unresolved_; }

	private:
		// One parsed .dat, order-preserving so BFC INVERTNEXT applies to the next ref.
		enum class Cmd { Ref, Tri, Quad, InvertNext };
		struct Command {
			Cmd       kind;
			glm::mat4 xf{ 1.0f };   // Ref: sub-file transform
			std::string ref;        // Ref: normalized sub-file name
			glm::vec3 v[4];         // Tri/Quad vertices
		};
		struct File {
			std::vector<Command> cmds;
			bool ccw = true;        // BFC winding default (true unless "BFC ... CW")
			// Connection classification of THIS file, if it is a stud primitive.
			bool isConnector = false;
			ConnectorType connType = ConnectorType::Male;
			ConnFamily connFamily = ConnFamily::None;
			int connDetents = 0;    // ClickHinge click-stop count, else 0
			std::string basename;   // e.g. "stud.dat"
		};

		// Parse (cached). Returns nullptr if unresolvable; the null is cached too.
		const File* getFile(const std::string& normName);
		std::unique_ptr<File> parseFile(const std::string& path, const std::string& basename);
		bool resolve(const std::string& normName, std::string& outPath) const;

		// insideConnector: true once we are recursing INSIDE a connector primitive, so
		// nested connectors aren't recorded again (axle holes nest sub-pieces also titled
		// "Axle Hole" — only the outermost counts as one connection). Geometry still emits.
		void bakeInto(const std::string& normName, const glm::mat4& xf,
		              bool invert, BakeResult& out, int depth, bool insideConnector);

		std::string root_;
		std::unordered_map<std::string, std::unique_ptr<File>> cache_;
		int unresolved_ = 0;
	};

} // namespace bagel::ldraw
