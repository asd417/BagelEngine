#pragma once
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

// Axis-aligned bounding box frustum culling.
// Planes are in the form dot(n, worldPos) + d >= 0 means inside.
struct Frustum {
    glm::vec4 planes[6];

    // Extract planes from a combined view-projection matrix.
    // Uses Gribb-Hartmann method, column-major GLM, Vulkan depth range [0, 1].
    void extractFromVP(const glm::mat4& VP) {
        planes[0] = { VP[0][3]+VP[0][0], VP[1][3]+VP[1][0], VP[2][3]+VP[2][0], VP[3][3]+VP[3][0] }; // left
        planes[1] = { VP[0][3]-VP[0][0], VP[1][3]-VP[1][0], VP[2][3]-VP[2][0], VP[3][3]-VP[3][0] }; // right
        planes[2] = { VP[0][3]+VP[0][1], VP[1][3]+VP[1][1], VP[2][3]+VP[2][1], VP[3][3]+VP[3][1] }; // bottom
        planes[3] = { VP[0][3]-VP[0][1], VP[1][3]-VP[1][1], VP[2][3]-VP[2][1], VP[3][3]-VP[3][1] }; // top
        planes[4] = { VP[0][2],          VP[1][2],          VP[2][2],          VP[3][2]          }; // near (z>=0 in Vulkan)
        planes[5] = { VP[0][3]-VP[0][2], VP[1][3]-VP[1][2], VP[2][3]-VP[2][2], VP[3][3]-VP[3][2] }; // far
    }

    // Returns false if the model-space AABB is fully outside the frustum (safe to cull).
    // Uses Arvo's method to transform the AABB to world space without computing all 8 corners.
    bool testAABB(const glm::vec3& bMin, const glm::vec3& bMax, const glm::mat4& M) const {
        glm::vec3 wMin = glm::vec3(M[3]);
        glm::vec3 wMax = glm::vec3(M[3]);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                float e = M[j][i] * bMin[j];
                float f = M[j][i] * bMax[j];
                if (e < f) { wMin[i] += e; wMax[i] += f; }
                else        { wMin[i] += f; wMax[i] += e; }
            }
        }
        for (int p = 0; p < 6; p++) {
            const glm::vec3 n(planes[p]);
            const glm::vec3 pv{
                n.x >= 0.0f ? wMax.x : wMin.x,
                n.y >= 0.0f ? wMax.y : wMin.y,
                n.z >= 0.0f ? wMax.z : wMin.z
            };
            if (glm::dot(n, pv) + planes[p].w < 0.0f) return false;
        }
        return true;
    }
};

} // namespace bagel
