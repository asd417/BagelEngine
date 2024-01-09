#include "bgl_camera.hpp"
#include <cassert>
#include <limits>

//https://www.youtube.com/watch?v=rvJHkYnAR3w&list=PL8327DO66nu9qYVKLDmdLW_84-yE4auCR&index=17&ab_channel=BrendanGalea

//https://johannesugb.github.io/gpu-programming/setting-up-a-proper-vulkan-projection-matrix/
const glm::mat4 xSpaceTransformMatrix {
    {1,0,0,0},
    {0,-1,0,0},
    {0,0,-1,0},
    {0,0,0,1}
};

void bagel::BGLCamera::setOrthographicProjection(float left, float right, float top, float bottom, float near, float far)
{
    glm::mat4 pTransformMat = glm::mat4{ 1.0f };
    pTransformMat[0][0] = 2.f / (right - left);
    pTransformMat[1][1] = 2.f / (bottom - top);
    pTransformMat[2][2] = 1.f / (far - near);
    pTransformMat[3][0] = -(right + left) / (right - left);
    pTransformMat[3][1] = -(bottom + top) / (bottom - top);
    pTransformMat[3][2] = -near / (far - near);
    //projectionMatrix = pTransformMat;
    projectionMatrix = pTransformMat * xSpaceTransformMatrix;
}

void bagel::BGLCamera::setPerspectiveProjection(float fovy, float aspect, float near, float far)
{
    assert(glm::abs(aspect - std::numeric_limits<float>::epsilon()) > 0.0f);
    const float tanHalfFovy = tan(fovy / 2.f);
    glm::mat4 pTransformMat = glm::mat4{ 0.0f };
    pTransformMat[0][0] = 1.f / (aspect * tanHalfFovy);
    pTransformMat[1][1] = 1.f / (tanHalfFovy);
    pTransformMat[2][2] = far / (far - near);
    pTransformMat[2][3] = 1.f;
    pTransformMat[3][2] = -(far * near) / (far - near);
    //projectionMatrix = pTransformMat;
    projectionMatrix = pTransformMat * xSpaceTransformMatrix;
}



void bagel::BGLCamera::setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up){
    // construct orthonormal basis
    // three unit vectors 
    const glm::vec3 w{ glm::normalize(direction) };
    const glm::vec3 u{ glm::normalize(glm::cross(w, up)) };
    const glm::vec3 v{ glm::cross(w, u) };
    setViewInverseView(position, u, v, w);
}

void bagel::BGLCamera::setViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up){
    setViewDirection(position, target - position, up);
}

void bagel::BGLCamera::setViewYXZ(glm::vec3 position, glm::vec3 rotation){
    const float c3 = glm::cos(rotation.z);
    const float s3 = glm::sin(rotation.z);
    const float c2 = glm::cos(rotation.x);
    const float s2 = glm::sin(rotation.x);
    const float c1 = glm::cos(rotation.y);
    const float s1 = glm::sin(rotation.y);
    const glm::vec3 u{ (c1 * c3 + s1 * s2 * s3), (c2 * s3), (c1 * s2 * s3 - c3 * s1) };
    const glm::vec3 v{ (c3 * s1 * s2 - c1 * s3), (c2 * c3), (c1 * c3 * s2 + s1 * s3) };
    const glm::vec3 w{ (c2 * s1), (-s2), (c1 * c2) };
    setViewInverseView(position, u, v, w);
}

bool bagel::BGLCamera::isPointInViewFrustum(glm::vec3 screenspaceCoord)
{
    
    // Point is inside or on the frustum
    return false;
}

void bagel::BGLCamera::setViewInverseView(glm::vec3 position, glm::vec3 u, glm::vec3 v, glm::vec3 w)
{
    viewMatrix = glm::mat4{ 1.f };
    viewMatrix[0][0] = u.x;
    viewMatrix[1][0] = u.y;
    viewMatrix[2][0] = u.z;
    viewMatrix[0][1] = v.x;
    viewMatrix[1][1] = v.y;
    viewMatrix[2][1] = v.z;
    viewMatrix[0][2] = w.x;
    viewMatrix[1][2] = w.y;
    viewMatrix[2][2] = w.z;
    viewMatrix[3][0] = -glm::dot(u, position);
    viewMatrix[3][1] = -glm::dot(v, position);
    viewMatrix[3][2] = -glm::dot(w, position);

    inverseViewMatrix = glm::mat4{ 1.f };
    inverseViewMatrix[0][0] = u.x;
    inverseViewMatrix[0][1] = u.y;
    inverseViewMatrix[0][2] = u.z;
    inverseViewMatrix[1][0] = v.x;
    inverseViewMatrix[1][1] = v.y;
    inverseViewMatrix[1][2] = v.z;
    inverseViewMatrix[2][0] = w.x;
    inverseViewMatrix[2][1] = w.y;
    inverseViewMatrix[2][2] = w.z;
    inverseViewMatrix[3][0] = position.x;
    inverseViewMatrix[3][1] = position.y;
    inverseViewMatrix[3][2] = position.z;
}
