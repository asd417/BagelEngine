#include "transform.hpp"
namespace bagel {
    
    // Cache the model matrix so render systems can read it via getMat4() without recomputing.
    void TransformComponent::cacheMat4()
    {
        cached = computeMat4();
    }

    //returns UNSCALED model matrix. Scale will be applied in shader
    glm::mat4 TransformComponent::computeMat4() const
    {
        //X1Y2Z3
        //Jolt Quaternion returns XYZ rotation
        const float c3 = glm::cos(rotation.z + localRotation.z);
        const float s3 = glm::sin(rotation.z + localRotation.z);
        const float c2 = glm::cos(rotation.y + localRotation.y);
        const float s2 = glm::sin(rotation.y + localRotation.y);
        const float c1 = glm::cos(rotation.x + localRotation.x);
        const float s1 = glm::sin(rotation.x + localRotation.x);

        return glm::mat4{
            {
                scale.x * localScale.x * (c2 * c3),
                scale.x * localScale.x * (c1 * s3 + c3 * s1 * s2),
                scale.x * localScale.x * (s1 * s3 - c1 * c3 * s2),
                0.0f,
            },
            {
                scale.y * localScale.y * (-c2 * s3),
                scale.y * localScale.y * (c1 * c3 - s1 * s2 * s3),
                scale.y * localScale.y * (c3 * s1 + c1 * s2 * s3),
                0.0f,
            },
            {
                scale.z * localScale.z * (s2),
                scale.z * localScale.z * (-c2 * s1),
                scale.z * localScale.z * (c1 * c2),
                0.0f,
            },
            {translation.x + localTranslation.x, translation.y + localTranslation.y, translation.z + localTranslation.z, 1.0f} };
    }
}