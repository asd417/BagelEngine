#include "ldraw_library.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace bagel::ldraw {

	namespace {
		std::string toLower(std::string s) {
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
		// Normalize an LDraw reference name: backslashes -> slashes, lowercased.
		std::string normalize(std::string s) {
			std::replace(s.begin(), s.end(), '\\', '/');
			return toLower(std::move(s));
		}
		std::string basenameOf(const std::string& n) {
			auto p = n.find_last_of('/');
			return p == std::string::npos ? n : n.substr(p + 1);
		}
		std::string trim(const std::string& s) {
			size_t a = s.find_first_not_of(" \t\r\n");
			if (a == std::string::npos) return "";
			size_t b = s.find_last_not_of(" \t\r\n");
			return s.substr(a, b - a + 1);
		}
		bool contains(const std::string& hay, const char* needle) {
			return hay.find(needle) != std::string::npos;
		}
		// Click-stop count from a Click-Lock-Hinge title, e.g.
		// "... Dual Finger 7-Position ..." -> 7. 0 if the title has no "N-Position".
		int parsePositionCount(const std::string& title) {
			size_t p = title.find("-Position");
			if (p == std::string::npos) return 0;
			size_t s = p;   // walk back over the digits immediately before the '-'
			while (s > 0 && std::isdigit(static_cast<unsigned char>(title[s - 1]))) --s;
			if (s == p) return 0;
			return std::stoi(title.substr(s, p - s));
		}
	}

	void assignAxisGroups(std::vector<ConnectionPoint>& conns) {
		constexpr float PARALLEL = 0.9995f;  // |dot| above this counts as parallel axes
		constexpr float LINE_EPS = 0.1f;     // LDU: perpendicular distance to the shared line
		                                     // (the nearest non-collinear case is ~20 LDU away)
		std::vector<size_t> repr;            // one representative connector index per group
		for (size_t i = 0; i < conns.size(); ++i) {
			const glm::vec3 axisI = glm::normalize(glm::vec3(conns[i].orient[1]));
			int g = -1;
			for (size_t k = 0; k < repr.size(); ++k) {
				const auto& rc = conns[repr[k]];
				const glm::vec3 axisR = glm::normalize(glm::vec3(rc.orient[1]));
				if (glm::abs(glm::dot(axisI, axisR)) < PARALLEL) continue;              // not parallel
				if (glm::length(glm::cross(conns[i].pos - rc.pos, axisR)) > LINE_EPS) continue; // off-line
				g = static_cast<int>(k);
				break;
			}
			if (g < 0) { g = static_cast<int>(repr.size()); repr.push_back(i); }
			conns[i].axisGroup = g;
		}
	}

	int BakeResult::maleCount() const {
		int n = 0; for (auto& c : connections) if (c.type == ConnectorType::Male) ++n; return n;
	}
	int BakeResult::femaleCount() const {
		int n = 0; for (auto& c : connections) if (c.type == ConnectorType::Female) ++n; return n;
	}
	int BakeResult::pinCount() const {
		int n = 0; for (auto& c : connections) if (c.type == ConnectorType::Pin) ++n; return n;
	}
	int BakeResult::axleCount() const {
		int n = 0; for (auto& c : connections) if (c.type == ConnectorType::Axle) ++n; return n;
	}

	bool Library::resolve(const std::string& normName, std::string& outPath) const {
		namespace fs = std::filesystem;
		// LDraw search order: parts/ then p/ then models/. The name may itself carry a
		// sub-directory (e.g. "s/3001s01.dat", "48/1-4cyli.dat").
		const char* dirs[] = { "parts/", "p/", "models/" };
		for (const char* d : dirs) {
			fs::path candidate = fs::path(root_) / (std::string(d) + normName);
			if (fs::exists(candidate)) { outPath = candidate.string(); return true; }
		}
		return false;
	}

	const Library::File* Library::getFile(const std::string& normName) {
		auto it = cache_.find(normName);
		if (it != cache_.end()) return it->second.get();

		std::string path;
		std::unique_ptr<File> file;
		if (resolve(normName, path))
			file = parseFile(path, basenameOf(normName));
		File* raw = file.get();
		cache_.emplace(normName, std::move(file)); // caches nullptr for unresolved
		return raw;
	}

	std::unique_ptr<Library::File> Library::parseFile(const std::string& path,
	                                                  const std::string& basename) {
		std::ifstream in(path);
		if (!in) return nullptr;

		auto file = std::make_unique<File>();
		file->basename = basename;

		std::string title;
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream ss(line);
			int type;
			if (!(ss >> type)) continue; // blank line

			switch (type) {
			case 0: {
				std::string rest;
				std::getline(ss, rest);
				rest = trim(rest);
				if (title.empty() && !rest.empty()) title = rest; // first 0-line = part title
				// BFC meta.
				if (rest.rfind("BFC", 0) == 0) {
					if (contains(rest, "INVERTNEXT")) {
						Command c; c.kind = Cmd::InvertNext; file->cmds.push_back(c);
					}
					// Winding default: CCW unless an explicit CW appears.
					if (contains(rest, "CW") && !contains(rest, "CCW")) file->ccw = false;
					else if (contains(rest, "CCW")) file->ccw = true;
				}
				break;
			}
			case 1: {
				std::string colorTok; ss >> colorTok;
				double m[12];
				bool ok = true;
				for (double& d : m) if (!(ss >> d)) { ok = false; break; }
				if (!ok) break;
				std::string name; std::getline(ss, name); name = normalize(trim(name));
				if (name.empty()) break;
				Command c; c.kind = Cmd::Ref; c.ref = name;
				// LDraw type-1: x y z  a b c  d e f  g h i  (row-major 3x3 + translation).
				// glm is column-major: column k is the image of basis vector k.
				c.xf = glm::mat4(
					m[3], m[6], m[9],  0.0f,   // col 0 = (a,d,g)
					m[4], m[7], m[10], 0.0f,   // col 1 = (b,e,h)
					m[5], m[8], m[11], 0.0f,   // col 2 = (c,f,i)
					m[0], m[1], m[2],  1.0f);  // col 3 = (x,y,z)
				file->cmds.push_back(std::move(c));
				break;
			}
			case 3:
			case 4: {
				std::string colorTok; ss >> colorTok;
				int nv = (type == 3) ? 3 : 4;
				Command c; c.kind = (type == 3) ? Cmd::Tri : Cmd::Quad;
				bool ok = true;
				for (int i = 0; i < nv; ++i) {
					double x, y, z;
					if (!(ss >> x >> y >> z)) { ok = false; break; }
					c.v[i] = glm::vec3(float(x), float(y), float(z));
				}
				if (ok) file->cmds.push_back(std::move(c));
				break;
			}
			default: break; // type 2 (edge) and 5 (optional line) ignored
			}
		}

		// Classify connector primitives from the title line. A connection point is
		// recorded wherever a part references one of these (see bakeInto). The hole
		// primitives nest sub-pieces titled "Peg Hole ..." (beamhole/connhole embed
		// peghole); classifying by the DISTINCT composite phrases below skips those
		// nested pieces, so a 1x5 beam reports 5 holes rather than double-counting.
		//   "Stud Tube ..."          -> Female  (underside receptacle)
		//   "Stud ..." (not Group)   -> Male    (raised stud)
		//   "Technic Axle Hole ..."  -> Axle    (cross-shaped, takes an axle)
		//   "... Beam Hole ..." /
		//   "... Connector Hole ..." -> Pin     (round, takes a pin)
		//   "joint8ball*"            -> Ball   / Joint8      (small towball, male)
		//   "joint8socket*"          -> Socket / Joint8      (small towball socket, female)
		//   "clh*" "Click Lock Hinge ... Single/Dual Finger [N-Position]"
		//                            -> Male/Female / ClickHinge (poseable, `detents` stops)
		// NOTE: the large Constraction/Bionicle balls and the raw-modeled Technic rotation
		// joints have NO dedicated primitive (they are built from generic sphere/cylinder
		// geometry), so they are NOT detected here -- see the per-part fallback in the design
		// note (src/lego/CONNECTORS.md).
		const std::string b = toLower(basename);
		if (b.rfind("stud", 0) == 0) {
			if (contains(title, "Tube")) { file->isConnector = true; file->connType = ConnectorType::Female; }
			else if (!contains(title, "Group")) { file->isConnector = true; file->connType = ConnectorType::Male; }
		}
		else if (contains(title, "Axle Hole")) {
			file->isConnector = true; file->connType = ConnectorType::Axle;
		}
		else if (contains(title, "Beam Hole") || contains(title, "Connector Hole")) {
			file->isConnector = true; file->connType = ConnectorType::Pin;
		}
		else if (b.rfind("joint8ball", 0) == 0) {
			file->isConnector = true; file->connType = ConnectorType::Ball;   file->connFamily = ConnFamily::Joint8;
		}
		else if (b.rfind("joint8socket", 0) == 0) {
			file->isConnector = true; file->connType = ConnectorType::Socket; file->connFamily = ConnFamily::Joint8;
		}
		else if (b.rfind("clh", 0) == 0) {
			// Click-lock hinge finger. Single finger = one half (Male), dual finger = the
			// other (Female); they share a hinge axis and click into `detents` stops. The
			// stop count "N-Position" is authored on the dual-finger titles (the single
			// finger inherits it from its mate at connection time).
			file->isConnector = true;
			file->connFamily = ConnFamily::ClickHinge;
			file->connType = contains(title, "Dual") ? ConnectorType::Female : ConnectorType::Male;
			file->connDetents = parsePositionCount(title);   // 0 if the title omits "N-Position"
		}
		return file;
	}

	void Library::bakeInto(const std::string& normName, const glm::mat4& xf,
	                       bool invert, BakeResult& out, int depth, bool insideConnector) {
		if (depth > 64) return; // cycle / runaway guard
		const File* file = getFile(normName);
		if (!file) { ++unresolved_; return; }

		// Winding parity: flipped by an odd number of negative-determinant transforms
		// and by an active INVERTNEXT. Determines outward normal direction (BFC-lite).
		const glm::mat3 basis(xf);
		const bool detNeg = glm::determinant(basis) < 0.0f;
		bool baseFlip = invert ^ detNeg ^ (!file->ccw);

		bool invertNext = false;
		for (const Command& c : file->cmds) {
			switch (c.kind) {
			case Cmd::InvertNext:
				invertNext = true;
				continue;
			case Cmd::Ref: {
				const glm::mat4 childXf = xf * c.xf;
				const File* child = getFile(c.ref);
				const bool childIsConn = child && child->isConnector;
				// Record only the outermost connector in a nested chain (see insideConnector).
				if (childIsConn && !insideConnector) {
					ConnectionPoint cp;
					cp.pos = glm::vec3(childXf[3]);
					cp.orient = glm::mat3(childXf);
					cp.type = child->connType;
					cp.family = child->connFamily;
					cp.detents = child->connDetents;
					cp.prim = child->basename;
					out.connections.push_back(std::move(cp));
				}
				bakeInto(c.ref, childXf, invert ^ invertNext, out, depth + 1,
				         insideConnector || childIsConn);
				invertNext = false;
				break;
			}
			case Cmd::Tri: {
				glm::vec3 a = glm::vec3(xf * glm::vec4(c.v[0], 1.0f));
				glm::vec3 b = glm::vec3(xf * glm::vec4(c.v[1], 1.0f));
				glm::vec3 d = glm::vec3(xf * glm::vec4(c.v[2], 1.0f));
				glm::vec3 n = glm::normalize(glm::cross(b - a, d - a));
				if (baseFlip) n = -n;
				uint32_t base = uint32_t(out.mesh.positions.size());
				out.mesh.positions.insert(out.mesh.positions.end(), { a, b, d });
				out.mesh.normals.insert(out.mesh.normals.end(), { n, n, n });
				// Reverse winding when the face is flipped so the emitted winding stays
				// consistent with the outward normal (matches the engine's CCW-front cull).
				if (baseFlip)
					out.mesh.indices.insert(out.mesh.indices.end(), { base, base + 2, base + 1 });
				else
					out.mesh.indices.insert(out.mesh.indices.end(), { base, base + 1, base + 2 });
				break;
			}
			case Cmd::Quad: {
				glm::vec3 a = glm::vec3(xf * glm::vec4(c.v[0], 1.0f));
				glm::vec3 b = glm::vec3(xf * glm::vec4(c.v[1], 1.0f));
				glm::vec3 d = glm::vec3(xf * glm::vec4(c.v[2], 1.0f));
				glm::vec3 e = glm::vec3(xf * glm::vec4(c.v[3], 1.0f));
				glm::vec3 n = glm::normalize(glm::cross(b - a, d - a));
				if (baseFlip) n = -n;
				uint32_t base = uint32_t(out.mesh.positions.size());
				out.mesh.positions.insert(out.mesh.positions.end(), { a, b, d, e });
				out.mesh.normals.insert(out.mesh.normals.end(), { n, n, n, n });
				// quad a,b,d,e -> triangles (a,b,d)+(a,d,e); reversed when the face is flipped
				// so the winding matches the outward normal (see the Tri case).
				if (baseFlip)
					out.mesh.indices.insert(out.mesh.indices.end(),
						{ base, base + 2, base + 1, base, base + 3, base + 2 });
				else
					out.mesh.indices.insert(out.mesh.indices.end(),
						{ base, base + 1, base + 2, base, base + 2, base + 3 });
				break;
			}
			}
		}
	}

	BakeResult Library::bake(const std::string& name) {
		unresolved_ = 0;
		std::string n = normalize(name);
		if (n.size() < 4 || n.substr(n.size() - 4) != ".dat") n += ".dat";
		BakeResult out;
		bakeInto(n, glm::mat4(1.0f), false, out, 0, false);
		return out;
	}

} // namespace bagel::ldraw
