#include "baked_connectors.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace bagel::ldraw {

	namespace {
		// Match the stem the baker named the file with: bare filename, no ".dat", lowercased,
		// backslashes -> slashes, directory stripped ("s/3001s01.dat" -> "3001s01").
		std::string keyOf(std::string s) {
			std::replace(s.begin(), s.end(), '\\', '/');
			auto slash = s.find_last_of('/');
			if (slash != std::string::npos) s = s.substr(slash + 1);
			if (s.size() >= 4) {
				std::string ext = s.substr(s.size() - 4);
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext == ".dat") s.resize(s.size() - 4);
			}
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		ConnectorType typeFromName(const std::string& s) {
			if (s == "female") return ConnectorType::Female;
			if (s == "pin")    return ConnectorType::Pin;
			if (s == "axle")   return ConnectorType::Axle;
			if (s == "ball")   return ConnectorType::Ball;
			if (s == "socket") return ConnectorType::Socket;
			return ConnectorType::Male;
		}

		ConnFamily familyFromName(const std::string& s) {
			if (s == "joint8")       return ConnFamily::Joint8;
			if (s == "constraction") return ConnFamily::Constraction;
			if (s == "clickhinge")   return ConnFamily::ClickHinge;
			if (s == "duplo")        return ConnFamily::Duplo;
			return ConnFamily::None;
		}

		// Build an orthonormal basis whose +Y (column 1) is the connector axis. Roll around
		// the axis is unconstrained by the file, so we pick any stable perpendicular frame --
		// the only consumer (the gizmo) uses just the axis direction.
		glm::mat3 basisFromAxis(glm::vec3 axis) {
			glm::vec3 y = glm::normalize(axis);
			glm::vec3 ref = (std::fabs(y.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
			glm::vec3 x = glm::normalize(glm::cross(ref, y));
			glm::vec3 z = glm::cross(y, x);
			return glm::mat3(x, y, z);   // columns
		}

		// Parse one <part>.conn file into connectors. Returns false only if the file cannot be
		// opened; malformed lines are skipped leniently. axisGroup is filled by the caller.
		bool parseFile(const std::string& path, std::vector<ConnectionPoint>& out) {
			std::ifstream in(path);
			if (!in) return false;
			std::string line;
			while (std::getline(in, line)) {
				size_t a = line.find_first_not_of(" \t\r\n");
				if (a == std::string::npos || line[a] == '#') continue;   // blank / comment
				std::istringstream ss(line);
				std::string type, family;
				int detents = 0;
				glm::vec3 pos, axis;
				if (!(ss >> type >> family >> detents
				         >> pos.x >> pos.y >> pos.z
				         >> axis.x >> axis.y >> axis.z))
					continue;   // malformed line -> skip
				std::transform(type.begin(), type.end(), type.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::transform(family.begin(), family.end(), family.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				ConnectionPoint cp;
				cp.pos     = pos;
				cp.orient  = basisFromAxis(axis);
				cp.type    = typeFromName(type);
				cp.family  = familyFromName(family);
				cp.detents = detents;
				out.push_back(std::move(cp));
			}
			return true;
		}
	}

	const std::vector<ConnectionPoint>* BakedConnectors::find(const std::string& partName) {
		const std::string key = keyOf(partName);
		if (auto it = cache_.find(key); it != cache_.end()) return &it->second;
		if (missing_.count(key)) return nullptr;

		const std::string path = (std::filesystem::path(dir_) / (key + ".conn")).string();
		std::vector<ConnectionPoint> conns;
		if (!parseFile(path, conns)) { missing_.insert(key); return nullptr; }

		assignAxisGroups(conns);   // derive collinearity groups (also covers hand-authored files)
		auto [it, _] = cache_.emplace(key, std::move(conns));
		return &it->second;
	}

} // namespace bagel::ldraw
