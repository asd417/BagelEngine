// Offline connector baker.
//
// Walks the LDraw parts library, bakes every part's LEGO connection points
// (studs / anti-stud tubes / pin holes / axle holes / Joint-8 balls+sockets /
// click-lock hinges) via bagel::ldraw::Library, and writes ONE human-readable text
// file per part so a human can hand author / fix connectors the baker can't detect
// (male axles/pins, big Constraction/Bionicle balls, raw-modeled rotation joints).
//
// Only the connectors are stored — the (expensive) flattened mesh that bake()
// also produces is discarded here. Parts with zero connectors get no file, so a
// missing <part>.conn simply means "no connectors" (the loader can still
// bake-on-miss as a fallback).
//
// Output: lego/baked/connections/<part>.conn -- plain text, one connector per line:
//   type family detents  px py pz  ax ay az
//     type    = male|female|pin|axle|ball|socket
//     family  = none|joint8|constraction|clickhinge|duplo
//     detents = click-hinge stop count ("N-Position"), else 0
//     pos     = part-local LDU;   ax..az = unit connector axis (+Y / stud direction)
//   Line 1 is "# source: baked"; change it to "# source: manual" and re-bakes skip
//   the file. axisGroup is NOT stored -- the engine derives it from geometry on load.
//   See src/lego/CONNECTORS.md for the full connector taxonomy.
//
// Build: run src/lego/partsParser/build_bake_connectors.bat (invokes g++; the Bash
// sandbox blocks g++ child processes, so use PowerShell/cmd). It compiles this file
// plus src/lego/ldraw_library.cpp into lego/partsParser.exe (the engine data dir).
//
// Usage:
//   partsParser [--root <ldraw-dir>]
//               [--no-connectors] [--skip-existing-connectors] [--connections-dir <dir>]
//               [--no-thumbnails] [--skip-done-thumbnail] [--thumb-size N] [--thumb-dir <dir>]
//               [--no-collision] [--skip-existing-collision | --skip-done-collision] [--collision-dir <dir>]
//               [--no-coacd] [--coacd-gate F] [--coacd-threshold F]
//               [--coacd-max-hulls N] [--coacd-resolution N] [--coacd-mcts-iter N]
//               [--no-coacd-decimate] [--coacd-max-ch-vertex N] [--no-coacd-real-metric]
//               [--python <exe>]
//   CoACD defaults: real-metric on (--coacd-threshold in LDU, default 20), decimate on
//   (--coacd-max-ch-vertex 64). Disable with --no-coacd-real-metric / --no-coacd-decimate.
//               [--test_thumbnail <id>] [--test_collision <id>]
//               [--concavity] [part names...]
//   With no part names, bakes every parts/*.dat top-level part.
//   Connector files are written by default to lego/baked/connections/<part>.conn.
//   Thumbnails are rendered by default: each part is drawn to
//   lego/baked/thumbnails/<part>.png via the CPU rasterizer, overwriting any
//   existing PNG. Pass --no-thumbnails to skip, or --skip-done-thumbnail to leave
//   parts whose PNG already exists untouched (resumable).
//   Collision hulls are baked by default: each part's convex hull is written to
//   lego/baked/collision/<part>.glb (binary glTF, raw LDU), overwriting any existing
//   file. Pass --no-collision to skip, or --skip-existing-collision to leave parts whose
//   GLB already exists untouched (resumable -- skips the costly CoACD step for done parts).
//   Parts whose volume-concavity exceeds --coacd-gate
//   (default 0.55) are decomposed into multiple convex hulls via CoACD (out-of-process
//   Python helper; run setup_coacd.bat/.sh first, or --no-coacd to disable). The list of
//   over-gate parts is written to lego/baked/high_concavity.txt.
//   --test_thumbnail / --test_collision <id> emit just that one part's PNG / GLB and exit.
//   --concavity <ids...> prints each named part's concavity (no files written).

#include "ldraw_library.hpp"

// This TU owns the stb_image_write implementation used by the thumbnail rasterizer.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "mesh_thumbnail.hpp"
#include "mesh_collision.hpp"   // convex-hull collision baking -> per-part .glb
#include "coacd_bake.hpp"       // CoACD convex decomposition for concave parts (Phase 2)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using bagel::ldraw::ConnectorType;

namespace {

	// Find the engine directory (the one that contains lego/ldraw) by walking up
	// from the current working directory, so the default paths work whether the
	// baker is launched from the engine root, from lego/, or from src/lego/.
	// Falls back to the cwd if no such ancestor is found.
	std::string engineDir() {
		fs::path cur = fs::current_path();
		for (fs::path p = cur; ; p = p.parent_path()) {
			if (fs::exists(p / "lego" / "ldraw")) return p.string();
			if (p == p.parent_path()) break;   // reached the filesystem root
		}
		return cur.string();
	}

	const char* typeName(ConnectorType t) {
		switch (t) {
			case ConnectorType::Male:   return "male";
			case ConnectorType::Female: return "female";
			case ConnectorType::Pin:    return "pin";
			case ConnectorType::Axle:   return "axle";
			case ConnectorType::Ball:   return "ball";
			case ConnectorType::Socket: return "socket";
		}
		return "male";
	}

	const char* familyName(bagel::ldraw::ConnFamily f) {
		switch (f) {
			case bagel::ldraw::ConnFamily::None:         return "none";
			case bagel::ldraw::ConnFamily::Joint8:       return "joint8";
			case bagel::ldraw::ConnFamily::Constraction: return "constraction";
			case bagel::ldraw::ConnFamily::ClickHinge:   return "clickhinge";
			case bagel::ldraw::ConnFamily::Duplo:        return "duplo";
		}
		return "none";
	}

	// A part whose <part>.conn line 1 marks it "# source: manual" has been hand-authored;
	// the baker must not overwrite it. (axisGroup is not written -- the engine derives it.)
	bool isManualConnFile(const fs::path& p) {
		std::ifstream f(p);
		std::string line;
		if (f && std::getline(f, line)) return line.find("source: manual") != std::string::npos;
		return false;
	}

	// Write a part's connectors as a per-part human-readable text file (see baked_connectors.hpp
	// for the format). Overwrites unless the existing file is marked "# source: manual".
	bool writeConnFile(const fs::path& path,
	                   const std::string& partName,
	                   const std::vector<bagel::ldraw::ConnectionPoint>& conns) {
		if (fs::exists(path) && isManualConnFile(path)) return false;   // preserve hand edits
		std::ofstream os(path, std::ios::trunc);
		if (!os) return false;
		os << "# source: baked\n"
		   << "# BagelEngine connectors -- part " << partName << "\n"
		   << "# fields: type family detents  px py pz  ax ay az   (axis = +Y/stud direction)\n";
		for (const auto& c : conns) {
			const glm::vec3 axis = glm::normalize(glm::vec3(c.orient[1]));   // +Y column
			os << typeName(c.type) << ' ' << familyName(c.family) << ' ' << c.detents << "  "
			   << c.pos.x << ' ' << c.pos.y << ' ' << c.pos.z << "  "
			   << axis.x << ' ' << axis.y << ' ' << axis.z << '\n';
		}
		return static_cast<bool>(os);
	}

	// LDraw sticker parts carry a title line whose description begins with "Sticker"
	// (e.g. "0 Sticker  2 x 2 ..."). The description may be prefixed with LDraw title
	// markers -- '~' hidden/moved, '=' alias, '_' raw colour, '|' -- e.g.
	// "0 =Sticker Minifig Torso ...", so strip those (and spaces) before testing.
	// Peeking the first non-empty line avoids a full parse.
	bool isSticker(const fs::path& datPath) {
		std::ifstream f(datPath);
		if (!f) return false;
		std::string line;
		while (std::getline(f, line)) {
			// Trim leading whitespace / UTF-8 BOM.
			size_t a = line.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
			if (a == std::string::npos) continue;      // blank line
			line = line.substr(a);
			if (line.empty() || line[0] != '0') return false;   // not a title line
			// Drop the leading "0", then any whitespace and title-marker chars.
			size_t d = line.find_first_not_of(" \t~=_|", 1);
			std::string desc = (d == std::string::npos) ? "" : line.substr(d);
			std::string head = desc.substr(0, 7);
			std::transform(head.begin(), head.end(), head.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return head == "sticker";
		}
		return false;
	}

	// A decorated variant is named "<base>p<code>" (printed: p01, p3c, pr0001, pat0001)
	// or "<base>d<code>" (applied sticker/decal: d01, d0b, ...), sitting on an optional
	// single mold-variant letter, e.g. 2528a+p3c, 2340+d0b. We treat it as decorated
	// only when the stripped base part actually exists in the library, so a stray p/d
	// can't misfire. `stem`/`allParts` are lowercased. Returns true (=> skip) for such.
	bool isDecoratedVariant(const std::string& stem, const std::unordered_set<std::string>& allParts) {
		static const std::regex re("^([0-9]+[a-z]?)[pd][0-9a-z]+$");
		std::smatch m;
		if (!std::regex_match(stem, m, re)) return false;
		return allParts.count(m[1].str()) > 0;
	}

	std::string toLowerStr(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

} // namespace

int main(int argc, char** argv) {
	std::string root;
	std::string connDir;                 // dir for per-part <part>.conn files
	bool connectors = true;              // connector files are written by default
	bool skipExistingConn = false;       // skip parts whose .conn already exists (resumable)
	std::string thumbDir;
	bool thumbnails = true;              // thumbnails are rendered by default
	bool skipExistingThumbnail = false; // skip parts whose PNG already exists (resumable)
	int thumbSize = 64;
	std::string testThumbnail;          // if set, render only this part's thumbnail and exit
	std::string collisionDir;
	bool collision = true;              // collision hulls are baked by default
	bool skipExistingCollision = false; // skip parts whose collision GLB already exists (resumable)
	std::string testCollision;          // if set, bake only this part's collision GLB and exit
	bool concavityMode = false;         // if set, print concavity for each named part and exit
	bool coacd = true;                  // decompose concave parts with CoACD (Phase 2)
	double coacdGate = 0.55;            // our volume-concavity above which we run CoACD
	double coacdThreshold = 20.0;       // CoACD concavity threshold; with real-metric on this
	                                    // is in LDU (~20 LDU ~ one module of allowed concavity)
	int coacdMaxHulls = -1;             // CoACD max_convex_hull (-1 = unlimited)
	int coacdResolution = 30;           // CoACD preprocess remesh resolution (lower = faster)
	int coacdMctsIter = 100;            // CoACD MCTS iterations (lower = faster)
	bool coacdDecimate = true;          // decimate each CoACD hull to <= coacdMaxChVertex verts
	int coacdMaxChVertex = 64;          // per-hull vertex cap when decimate is on
	bool coacdRealMetric = true;        // --coacd-threshold is in LDU (not normalized concavity)
	std::string pythonOverride;         // override the venv interpreter
	std::vector<std::string> explicitParts;

	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--root" && i + 1 < argc)                root = argv[++i];
		else if (a == "--connections-dir" && i + 1 < argc) connDir = argv[++i];
		else if (a == "--no-connectors")                  connectors = false;
		else if (a == "--skip-existing-connectors")       skipExistingConn = true;
		else if (a == "--no-thumbnails")                  thumbnails = false;
		else if (a == "--thumbnails")                     thumbnails = true;   // back-compat no-op
		else if (a == "--skip-done-thumbnail")            skipExistingThumbnail = true;
		else if (a == "--thumb-size" && i + 1 < argc)     thumbSize = std::max(1, std::atoi(argv[++i]));
		else if (a == "--thumb-dir" && i + 1 < argc)      thumbDir = argv[++i];
		else if (a == "--test_thumbnail" && i + 1 < argc) testThumbnail = argv[++i];
		else if (a == "--no-collision")                   collision = false;
		else if (a == "--skip-existing-collision")        skipExistingCollision = true;
		else if (a == "--skip-done-collision")            skipExistingCollision = true;   // alias
		else if (a == "--collision-dir" && i + 1 < argc)  collisionDir = argv[++i];
		else if (a == "--test_collision" && i + 1 < argc) testCollision = argv[++i];
		else if (a == "--concavity")                      concavityMode = true;
		else if (a == "--no-coacd")                       coacd = false;
		else if (a == "--coacd-gate" && i + 1 < argc)     coacdGate = std::atof(argv[++i]);
		else if (a == "--coacd-threshold" && i + 1 < argc) coacdThreshold = std::atof(argv[++i]);
		else if (a == "--coacd-max-hulls" && i + 1 < argc) coacdMaxHulls = std::atoi(argv[++i]);
		else if (a == "--coacd-resolution" && i + 1 < argc) coacdResolution = std::atoi(argv[++i]);
		else if (a == "--coacd-mcts-iter" && i + 1 < argc) coacdMctsIter = std::atoi(argv[++i]);
		else if (a == "--coacd-decimate")                 coacdDecimate = true;
		else if (a == "--no-coacd-decimate")              coacdDecimate = false;
		else if (a == "--coacd-max-ch-vertex" && i + 1 < argc) coacdMaxChVertex = std::atoi(argv[++i]);
		else if (a == "--coacd-real-metric")              coacdRealMetric = true;
		else if (a == "--no-coacd-real-metric")           coacdRealMetric = false;
		else if (a == "--python" && i + 1 < argc)         pythonOverride = argv[++i];
		else                                              explicitParts.push_back(a);
	}

	const std::string engine = engineDir();
	if (root.empty())     root     = (fs::path(engine) / "lego" / "ldraw").string();
	if (connDir.empty())  connDir  = (fs::path(engine) / "lego" / "baked" / "connections").string();
	if (thumbDir.empty()) thumbDir = (fs::path(engine) / "lego" / "baked" / "thumbnails").string();
	if (collisionDir.empty()) collisionDir = (fs::path(engine) / "lego" / "baked" / "collision").string();

	// CoACD config: the venv interpreter + helper next to this tool's source. If CoACD is
	// requested but the venv is missing, warn once and degrade to single-hull baking.
	bagel::ldraw::CoacdConfig coacdCfg;
	coacdCfg.helper = (fs::path(engine) / "lego" / "coacd_decompose.py").string();
	coacdCfg.threshold = coacdThreshold;
	coacdCfg.maxHulls = coacdMaxHulls;
	coacdCfg.resolution = coacdResolution;
	coacdCfg.mctsIter = coacdMctsIter;
	coacdCfg.decimate = coacdDecimate;
	coacdCfg.maxChVertex = coacdMaxChVertex;
	coacdCfg.realMetric = coacdRealMetric;
	{
#ifdef _WIN32
		const std::string venvPy = (fs::path(engine) / "lego" / ".venv" / "Scripts" / "python.exe").string();
#else
		const std::string venvPy = (fs::path(engine) / "lego" / ".venv" / "bin" / "python").string();
#endif
		coacdCfg.python = pythonOverride.empty() ? venvPy : pythonOverride;
		if (coacd && !fs::exists(coacdCfg.python)) {
			std::cerr << "[bake] CoACD interpreter not found: " << coacdCfg.python << "\n"
			          << "       run lego/setup_coacd.bat (or pass --python), or --no-coacd.\n"
			          << "       Falling back to single convex hulls for concave parts.\n";
			coacd = false;
		}
	}

	// Names + concavity of parts that exceeded the gate (reported to a text file at the end).
	struct ConcaveEntry { std::string name; double cc; size_t hulls; };
	std::vector<ConcaveEntry> highConcavity;

	// Build a part's collision hull(s): single convex hull, or -- for parts whose volume
	// concavity exceeds the gate -- a CoACD decomposition (each piece re-hulled tight).
	// `outCc` receives the measured concavity so the caller can log the high ones.
	auto bakeHulls = [&](const bagel::ldraw::BakeResult& r, const std::string& name, double& outCc) {
		std::vector<bagel::ldraw::CollisionHull> out;
		outCc = r.mesh.positions.empty() ? 0.0 : bagel::ldraw::concavity(r.mesh);
		if (coacd && outCc > coacdGate) {
			bool ok = false;
			out = bagel::ldraw::coacdHulls(r.mesh, name, coacdCfg, ok);
			if (ok) return out;
			std::cerr << "[bake] CoACD failed for " << name << " -> single hull fallback\n";
		}
		out = { bagel::ldraw::computeHull(r.mesh) };
		return out;
	};

	if (!fs::exists(root)) {
		std::cerr << "[bake] LDraw root not found: " << root << "\n"
		          << "       pass --root <dir> (the folder holding parts/ and p/)\n";
		return 1;
	}

	// --test_thumbnail: render a single part's thumbnail and exit, skipping the
	// connector bake / connectors.bin write entirely (fast style iteration).
	if (!testThumbnail.empty()) {
		fs::create_directories(thumbDir);
		bagel::ldraw::Library lib(root);
		auto r = lib.bake(testThumbnail);
		const std::string png = (fs::path(thumbDir) / (testThumbnail + ".png")).string();
		if (bagel::ldraw::renderThumbnail(r.mesh, thumbSize, png)) {
			std::cout << "[bake] test thumbnail -> " << png << "\n";
			return 0;
		}
		std::cerr << "[bake] test thumbnail FAILED for: " << testThumbnail
		          << " (empty mesh? check the part id)\n";
		return 1;
	}

	// --test_collision: bake a single part's collision GLB and exit (no connector bake).
	if (!testCollision.empty()) {
		fs::create_directories(collisionDir);
		bagel::ldraw::Library lib(root);
		auto r = lib.bake(testCollision);
		double cc = 0.0;
		std::vector<bagel::ldraw::CollisionHull> hulls = bakeHulls(r, testCollision, cc);
		const std::string glb = (fs::path(collisionDir) / (testCollision + ".glb")).string();
		if (bagel::ldraw::writeCollisionGlb(hulls, glb)) {
			size_t tris = 0; for (const auto& h : hulls) tris += h.indices.size() / 3;
			std::cout << "[bake] test collision -> " << glb << "  ("
			          << hulls.size() << " hull(s), " << tris << " tris, concavity "
			          << cc << ")\n";
			return 0;
		}
		std::cerr << "[bake] test collision FAILED for: " << testCollision
		          << " (empty mesh? check the part id)\n";
		return 1;
	}

	// --concavity: report mesh vs convex-hull volume for each named part and exit.
	// Concavity in [0,1] = 1 - meshVol/hullVol (0 = convex once holes are filled).
	if (concavityMode) {
		bagel::ldraw::Library lib(root);
		std::cout << "part        concavity   meshVol(LDU^3)   hullVol(LDU^3)\n";
		for (const std::string& name : explicitParts) {
			auto r = lib.bake(name);
			double mv = 0.0, hv = 0.0;
			const double cc = bagel::ldraw::concavity(r.mesh, &mv, &hv);
			char line[160];
			std::snprintf(line, sizeof(line), "%-10s  %8.4f   %14.1f   %14.1f\n",
			              name.c_str(), cc, mv, hv);
			std::cout << line;
		}
		return 0;
	}

	// Build the work list: explicit names, or every top-level parts/*.dat.
	std::vector<std::string> partNames;
	if (!explicitParts.empty()) {
		partNames = explicitParts;
	} else {
		const fs::path partsDir = fs::path(root) / "parts";
		if (!fs::exists(partsDir)) {
			std::cerr << "[bake] no parts/ under root: " << partsDir.string() << "\n";
			return 1;
		}
		// First pass: gather every .dat so decorated-variant detection can check whether
		// a candidate base part actually exists.
		std::vector<fs::path> datFiles;
		std::unordered_set<std::string> allParts;
		for (const auto& e : fs::directory_iterator(partsDir)) {
			if (!e.is_regular_file()) continue;
			const fs::path& p = e.path();
			if (toLowerStr(p.extension().string()) != ".dat") continue;
			datFiles.push_back(p);
			allParts.insert(toLowerStr(p.stem().string()));
		}

		// Second pass: drop stickers and decorated (printed / embedded-sticker) variants.
		size_t stickersSkipped = 0, decoratedSkipped = 0;
		for (const fs::path& p : datFiles) {
			const std::string stem = toLowerStr(p.stem().string());
			if (isSticker(p)) { ++stickersSkipped; continue; }
			if (isDecoratedVariant(stem, allParts)) { ++decoratedSkipped; continue; }
			partNames.push_back(p.stem().string());
		}
		std::sort(partNames.begin(), partNames.end());
		std::cout << "[bake] skipped " << stickersSkipped << " sticker + "
		          << decoratedSkipped << " decorated (printed) parts\n";
	}

	std::cout << "[bake] root:  " << root << "\n"
	          << "[bake] parts: " << partNames.size() << "\n";
	if (connectors) {
		fs::create_directories(connDir);
		std::cout << "[bake] connections: " << connDir << " (per-part .conn text)\n";
	}
	if (thumbnails) {
		fs::create_directories(thumbDir);
		std::cout << "[bake] thumbs: " << thumbDir << " (" << thumbSize << "px)\n";
	}
	if (collision) {
		fs::create_directories(collisionDir);
		std::cout << "[bake] collision: " << collisionDir << " (.glb convex hulls"
		          << (coacd ? ", CoACD > " + std::to_string(coacdGate) : ", CoACD off") << ")\n";
	}

	bagel::ldraw::Library lib(root);

	size_t totalConns = 0;
	size_t connWritten = 0;
	size_t connSkipped = 0;
	size_t thumbsWritten = 0;
	size_t thumbsSkipped = 0;
	size_t collWritten = 0;
	size_t collSkipped = 0;
	size_t processed = 0;
	for (const std::string& name : partNames) {
		auto r = lib.bake(name);

		// Thumbnail: render the flattened mesh (present in r.mesh before we discard it),
		// overwriting any existing PNG unless --skip-done-thumbnail leaves done ones alone.
		if (thumbnails) {
			const std::string png = (fs::path(thumbDir) / (name + ".png")).string();
			if (skipExistingThumbnail && fs::exists(png)) ++thumbsSkipped;
			else if (bagel::ldraw::renderThumbnail(r.mesh, thumbSize, png)) ++thumbsWritten;
		}

		// Collision: convex hull (or CoACD decomposition if concave) -> per-part GLB,
		// overwriting any existing. Parts over the concavity gate are logged for the report.
		// With --skip-existing-collision a part whose GLB already exists is skipped BEFORE
		// the (expensive) CoACD step, so an interrupted run resumes cheaply. Skipped parts
		// aren't re-measured, so they won't appear in the concavity report on a resume.
		if (collision && !r.mesh.positions.empty()) {
			const std::string glb = (fs::path(collisionDir) / (name + ".glb")).string();
			if (skipExistingCollision && fs::exists(glb)) {
				++collSkipped;
			} else {
				double cc = 0.0;
				std::vector<bagel::ldraw::CollisionHull> hulls = bakeHulls(r, name, cc);
				if (bagel::ldraw::writeCollisionGlb(hulls, glb)) ++collWritten;
				if (cc > coacdGate) highConcavity.push_back({ name, cc, hulls.size() });
			}
		}

		// Connectors: one human-readable <part>.conn per part (only parts that have any).
		// Files marked "# source: manual" are preserved; --skip-existing-connectors leaves
		// any existing file untouched (resumable).
		if (connectors && !r.connections.empty()) {
			totalConns += r.connections.size();
			const fs::path conn = fs::path(connDir) / (name + ".conn");
			if (skipExistingConn && fs::exists(conn)) ++connSkipped;
			else if (writeConnFile(conn, name, r.connections)) ++connWritten;
			else ++connSkipped;   // manual-locked or write failure
		}
		if (++processed % 1000 == 0) {
			std::cout << "[bake] " << processed << "/" << partNames.size()
			          << " (" << connWritten << " conn";
			if (thumbnails) std::cout << ", " << thumbsWritten << " thumbs";
			if (collision)  std::cout << ", " << collWritten << " collision";
			std::cout << ")\n";
		}
	}

	std::cout << "[bake] done: " << connWritten << " connector files, "
	          << totalConns << " connectors";
	if (skipExistingConn || connSkipped) std::cout << ", " << connSkipped << " skipped (existing/manual)";
	std::cout << " -> " << connDir << "\n";
	if (thumbnails) {
		std::cout << "[bake] thumbs: " << thumbsWritten << " written";
		if (skipExistingThumbnail) std::cout << ", " << thumbsSkipped << " skipped (existing)";
		std::cout << " -> " << thumbDir << "\n";
	}
	if (collision) {
		std::cout << "[bake] collision: " << collWritten << " written";
		if (skipExistingCollision) std::cout << ", " << collSkipped << " skipped (existing)";
		std::cout << " -> " << collisionDir << "\n";
	}

	// Report the high-concavity parts (over the gate -> got CoACD, or would have) to a
	// text file, sorted most-concave first.
	if (collision) {
		std::sort(highConcavity.begin(), highConcavity.end(),
		          [](const ConcaveEntry& a, const ConcaveEntry& b) { return a.cc > b.cc; });
		const std::string reportPath = (fs::path(collisionDir).parent_path() / "high_concavity.txt").string();
		std::ofstream rep(reportPath, std::ios::trunc);
		if (rep) {
			rep << "# Parts with volume-concavity > " << coacdGate
			    << (coacd ? " (decomposed via CoACD)" : " (CoACD disabled -- single hull used)") << "\n";
			rep << "# part\tconcavity\thulls\n";
			for (const ConcaveEntry& e : highConcavity) {
				char line[160];
				std::snprintf(line, sizeof(line), "%s\t%.4f\t%zu\n", e.name.c_str(), e.cc, e.hulls);
				rep << line;
			}
			std::cout << "[bake] concavity: " << highConcavity.size() << " parts > " << coacdGate
			          << " -> " << reportPath << "\n";
		} else {
			std::cerr << "[bake] could not write concavity report: " << reportPath << "\n";
		}
	}
	return 0;
}
