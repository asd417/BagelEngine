#pragma once
#include "connector_types.hpp"          // shared ConnectorType / ConnFamily / MateType / JointKind
#include "../bagel_connection_graph.hpp" // generic engine-level ConnectionGraph<Edge>
#include <entt.hpp>
#include <cstdint>
#include <vector>
namespace bagel
{
    // hole (female)
    // axle slot (female)
    // pin (male)
    // axle (male)
    // brick male
    // brick female
    // handle (male) (things that can be held in minifigure hand. can be stuck in axle slot)

    // Connector enums (MateType, ConnFamily, JointKind, ...) are defined once in
    // connector_types.hpp and shared with the ldraw parser; pull them into `bagel` so the
    // graph can name them (and their enumerators) unqualified.
    using namespace ldraw;

    // One directed edge in the LEGO connection graph: the LEGO-specific payload the generic
    // ConnectionGraph carries. `other` (required by ConnectionGraph) names the far part.
    struct Connection
    {
        entt::entity other;          // the part on the far end of this edge
        uint8_t connector;           // index of THIS part's connector (into its baked list)
        uint8_t connectorOther;      // index of `other`'s connector
        uint8_t axisGroup;           // axisGroup of THIS part's `connector` (for collinearity)
        MateType type;
        ConnFamily family;           // mechanical family of the connection (ball system, hinge, ...)
    };

    // LEGO-specific view over the generic entity graph: stores connector mate data on each edge
    // and adds joint classification + solid-group binning. The storage, connected-component logic,
    // and lifecycle live in bagel::ConnectionGraph (engine-generic); everything LEGO — the edge
    // payload, resolveJoint, and "solid = rigidly welded" — lives here.
    class LegoConnectionGraph
    {
    public:
        // Record a connection between two parts' connectors. Stores both directions; each
        // direction carries that side's own connector index + axisGroup.
        void addConnection(entt::entity my, uint8_t connector, uint8_t axisGroup,
                           entt::entity other, uint8_t connectorOther, uint8_t axisGroupOther,
                           MateType type, ConnFamily family)
        {
            graph.addConnection(
                my,    Connection{other, connector, connectorOther, axisGroup,      type, family},
                other, Connection{my,    connectorOther, connector, axisGroupOther, type, family});
        }
        // remove a specific edge (both directions)
        void removeConnection(entt::entity my, entt::entity other) { graph.removeConnection(my, other); }
        // remove ALL edges of an entity — call this from registry.on_destroy<BrickTag>
        void removeEntity(entt::entity e) { graph.removeEntity(e); }
        const std::vector<Connection> *neighbors(entt::entity e) const { return graph.neighbors(e); }
        bool areConnected(entt::entity a, entt::entity b) const { return graph.areConnected(a, b); }

        // Classify the joint between `a` and `b` from all connections they share. Pure graph
        // data — no geometry lookup, since each edge carries its axisGroup. Order matters:
        // ball and click-hinge win over the stud/axle collinearity logic.
        JointKind resolveJoint(entt::entity a, entt::entity b) const
        {
            const std::vector<Connection> *adj = graph.neighbors(a);
            if (adj == nullptr)
                return JointKind::None;

            int n = 0, group = -1;
            bool sameGroup = true, hasBall = false, hasClick = false, hasAxleslot = false;
            for (const Connection &c : *adj)
            {
                if (c.other != b)
                    continue;
                if (c.type == BALL_SOCKET)   hasBall = true;
                if (c.type == CLICK_HINGE)   hasClick = true;
                if (c.type == AXLE_AXLESLOT) hasAxleslot = true;
                if (n == 0) group = c.axisGroup;
                else if (c.axisGroup != group) sameGroup = false;
                ++n;
            }

            if (n == 0)      return JointKind::None;
            if (hasBall)     return JointKind::Ball;        // 3-DOF, physics owns it
            if (hasClick)    return JointKind::ClickHinge;  // detented hinge
            if (hasAxleslot) return JointKind::Rigid;       // keyed axle locks the pair
            if (n == 1)      return JointKind::Hinge;        // single rotatable point
            return sameGroup ? JointKind::Hinge              // collinear -> free spin
                             : JointKind::Rigid;             // offset-parallel -> welded
        }

        // Partition every part into "solid groups": maximal sets welded together through Rigid
        // joints. Parts joined only by movable joints (Hinge / ClickHinge / Ball) land in different
        // groups — each solid group is meant to become one rigid body, with the movable joints
        // becoming constraints between groups. Parts with no edges aren't returned; the caller
        // treats each such isolated part as its own singleton group. Feeds BGLJolt::BuildBodiesPerGroup.
        std::vector<std::vector<entt::entity>> binSolidGroups() const
        {
            return graph.binGroups([this](entt::entity a, entt::entity b) {
                return resolveJoint(a, b) == JointKind::Rigid;
            });
        }

    private:
        ConnectionGraph<Connection> graph;   // generic engine-level connectivity + grouping
    };
}
