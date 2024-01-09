#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
//#include <math.h>
namespace bagel {
	//Uses Quaternions to calculate look vector pointing from origin to lookTarget
	//up is {0,1,0} by default
	//alternateUp is used when lookTarget - pos is almost parallel to up vector. {1,0,0} by default
	glm::vec3 GetLookVector(glm::vec3 origin, glm::vec3 lookTarget, glm::vec3 up = {0,1,0}, glm::vec3 alternateUp = { 1,0,0 }) {
		glm::vec3 dirVec = glm::normalize(lookTarget - origin);

		glm::qua<float> qrot;
		if (glm::abs(glm::dot(dirVec, up)) > .9999f)	qrot = glm::quatLookAt(-dirVec, alternateUp);
		else											qrot = glm::quatLookAt(-dirVec, up);
		// OpenGL's forward vector is -z
		qrot.z = qrot.z * -1;
		return glm::eulerAngles(qrot);
	}
}