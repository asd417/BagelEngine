#pragma once
#include "connector_types.hpp"   // shared ConnectorType / ConnFamily / MateType / JointKind
#include <entt.hpp>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
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

    struct Connection
    {
        entt::entity other;          // the part on the far end of this edge
        uint8_t connector;           // index of THIS part's connector (into its baked list)
        uint8_t connectorOther;      // index of `other`'s connector
        uint8_t axisGroup;           // axisGroup of THIS part's `connector` (for collinearity)
        MateType type;
        ConnFamily family;           // mechanical family of the connection (ball system, hinge, ...)
    };
    // connections between all parts
    // used to resolve physics hinge settings
    class ConnectionsGraph
    {
    public:
        // Record a connection between two parts' connectors. Stores both directions; each
        // direction carries that side's own connector index + axisGroup.
        void addConnection(entt::entity my, uint8_t connector, uint8_t axisGroup,
                           entt::entity other, uint8_t connectorOther, uint8_t axisGroupOther,
                           MateType type, ConnFamily family)
        {
            connections[my].push_back({other, connector, connectorOther, axisGroup, type, family});
            connections[other].push_back({my, connectorOther, connector, axisGroupOther, type, family});
        }
        // remove a specific edge (both directions)
        void removeConnection(entt::entity my, entt::entity other)
        {
            stripEdges(my, other);
            stripEdges(other, my);
        }
        // remove ALL edges of an entity — call this from registry.on_destroy<BrickTag>
        void removeEntity(entt::entity e)
        {
            auto it = connections.find(e);
            if (it == connections.end())
                return;
            for (const Connection &c : it->second) // strip only the mirror side while we
                stripEdges(c.other, e);            // still hold e's list intact
            connections.erase(e);                  // then drop e's list in one shot
        }
        const std::vector<Connection> *neighbors(entt::entity e) const
        {
            auto it = connections.find(e);
            return it == connections.end() ? nullptr : &it->second;
        }
        bool areConnected(entt::entity a, entt::entity b) const
        {
            if (auto it = connections.find(a); it != connections.end())
                for (const Connection &c : it->second)
                    if (c.other == b)
                        return true;
            return false;
        }
        // Classify the joint between `a` and `b` from all connections they share. Pure graph
        // data — no geometry lookup, since each edge carries its axisGroup. Order matters:
        // ball and click-hinge win over the stud/axle collinearity logic.
        JointKind resolveJoint(entt::entity a, entt::entity b) const
        {
            auto it = connections.find(a);
            if (it == connections.end())
                return JointKind::None;

            int n = 0, group = -1;
            bool sameGroup = true, hasBall = false, hasClick = false, hasAxleslot = false;
            for (const Connection &c : it->second)
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

    private:
        // drop every edge in a's list that points at b
        void stripEdges(entt::entity a, entt::entity b)
        {
            auto it = connections.find(a);
            if (it == connections.end())
                return;
            auto &v = it->second;
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [b](const Connection &c)
                                   { return c.other == b; }),
                    v.end());
        }
        std::unordered_map<entt::entity, std::vector<Connection>> connections;
    };
}
