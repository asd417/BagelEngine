#pragma once
// Generic, engine-level connectivity graph over ECS entities. It stores an undirected adjacency
// of entities and computes connected components; it is deliberately domain-agnostic. What a
// "connection" *means* (and which connections should group together) is supplied by the caller
// via the edge payload and a grouping predicate.

// This is useful for simply creating relations between entities without creating hierarchy

#include <entt.hpp>
#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace bagel {

	// Undirected graph over entities. `Edge` is an application-defined payload that MUST expose an
	// `entt::entity other` member naming the far endpoint; any additional fields are opaque here.
	template <class Edge>
	class ConnectionGraph {
	public:
		// Record an edge between `my` and `other`. Stored in both directions so either endpoint can
		// enumerate it; each side carries its own payload (per-side fields may differ). By contract
		// myEdge.other == other and otherEdge.other == my.
		void addConnection(entt::entity my, const Edge &myEdge, entt::entity other, const Edge &otherEdge)
		{
			adjacency[my].push_back(myEdge);
			adjacency[other].push_back(otherEdge);
		}

		// Remove a specific undirected edge (both directions).
		void removeConnection(entt::entity a, entt::entity b)
		{
			stripEdges(a, b);
			stripEdges(b, a);
		}

		// Remove all edges touching `e` (call from registry.on_destroy of the tag you key on).
		void removeEntity(entt::entity e)
		{
			auto it = adjacency.find(e);
			if (it == adjacency.end())
				return;
			for (const Edge &c : it->second) // strip the mirror side while e's list is intact
				stripEdges(c.other, e);
			adjacency.erase(e);
		}

		const std::vector<Edge> *neighbors(entt::entity e) const
		{
			auto it = adjacency.find(e);
			return it == adjacency.end() ? nullptr : &it->second;
		}

		bool areConnected(entt::entity a, entt::entity b) const
		{
			if (auto it = adjacency.find(a); it != adjacency.end())
				for (const Edge &c : it->second)
					if (c.other == b)
						return true;
			return false;
		}

		// Partition every entity in the graph into components, unioning a pair (a, b) only when
		// `shouldGroup(a, b)` is true. Pass an always-true predicate for plain connected components,
		// or a stricter one (e.g. "rigidly joined") for solid groups. Entities with no edges never
		// enter the graph, so they are not returned; the caller treats each as its own singleton.
		template <class Pred>
		std::vector<std::vector<entt::entity>> binGroups(Pred shouldGroup) const
		{
			// Union-find over the entities present in the graph.
			std::unordered_map<entt::entity, entt::entity> parent;
			for (const auto &kv : adjacency)
				parent.emplace(kv.first, kv.first);

			auto find = [&](entt::entity x) {
				while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } // path halving
				return x;
			};
			auto unite = [&](entt::entity a, entt::entity b) {
				entt::entity ra = find(a), rb = find(b);
				if (ra != rb) parent[ra] = rb;
			};

			// Union across accepted edges; visit each unordered pair once (ordered by id).
			for (const auto &kv : adjacency)
				for (const Edge &c : kv.second)
					if (entt::to_integral(kv.first) < entt::to_integral(c.other) && shouldGroup(kv.first, c.other))
						unite(kv.first, c.other);

			// Bucket entities by component root, in first-seen order.
			std::unordered_map<entt::entity, std::size_t> rootToBin;
			std::vector<std::vector<entt::entity>> bins;
			for (const auto &kv : adjacency) {
				entt::entity r = find(kv.first);
				auto [it, inserted] = rootToBin.emplace(r, bins.size());
				if (inserted) bins.emplace_back();
				bins[it->second].push_back(kv.first);
			}
			return bins;
		}

	private:
		void stripEdges(entt::entity a, entt::entity b)
		{
			auto it = adjacency.find(a);
			if (it == adjacency.end())
				return;
			auto &v = it->second;
			v.erase(std::remove_if(v.begin(), v.end(),
			                       [b](const Edge &c) { return c.other == b; }),
			        v.end());
		}

		std::unordered_map<entt::entity, std::vector<Edge>> adjacency;
	};

} // namespace bagel
