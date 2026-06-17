#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

#include "entt.hpp"
#include "bagel_camera.hpp"

struct GLFWwindow;

namespace bagel {

	// In-game bone posing gizmo: selection + interaction state, no Vulkan.
	//
	// Drives manual posing of a skinned entity. Each frame update() reads mouse/keyboard,
	// (re)resolves the target's joint world transforms, lets the user click a joint handle to
	// select it, and drag a translate arrow / rotate ring to edit that joint's local TRS in the
	// AnimationComponent::editPose (setting poseDirty so the engine re-uploads the palette).
	//
	// Companion GizmoRenderSystem reads this object's getters to draw the handles.
	enum class GizmoMode { Translate, Rotate };

	class PoseGizmo {
	public:
		explicit PoseGizmo(entt::registry& reg) : registry{ reg } {}

		// Per-frame tick. `vpW/vpH` are the viewport pixel dimensions (for the mouse ray).
		// Toggling edit mode (G) latches onto the first skinned AnimationComponent entity and
		// flips its manualPose on so draws read the live edited pose.
		void update(GLFWwindow* window, const BGLCamera& camera, float vpW, float vpH);

		// Programmatic edit-mode control (e.g. the `editmode 0/1` console command). Same effect
		// as the G hotkey: enabling latches the target skinned entity.
		void setEditMode(bool on);
		bool editModeOn() const { return editMode; }

		// ---- read by GizmoRenderSystem ----
		bool          active()                const { return editMode && target != entt::null; }
		entt::entity  targetEntity()          const { return target; }
		int           selectedJoint()         const { return selJoint; }
		GizmoMode     mode()                  const { return gmode; }
		int           hoveredAxis()           const { return hoverAxis; }  // -1 none, 0/1/2 = X/Y/Z
		int           draggingAxis()          const { return dragAxis; }   // -1 none
		bool          localSpaceOn()          const { return localSpace; } // true = bone-local axes
		// Orientation the handles are drawn with: identity in global space, the selected bone's
		// orthonormal rotation in local space.
		glm::mat4     handleBasis()           const;
		glm::vec3     selectedJointWorldPos()  const { return selJointWorldPos; }
		// Full world matrix of the selected joint (its basis columns are the bone's local axes).
		glm::mat4     selectedJointWorldMatrix() const {
			return (selJoint >= 0 && selJoint < static_cast<int>(globals.size()))
				? entityModel * globals[selJoint] : glm::mat4{ 1.0f };
		}
		const std::vector<glm::vec3>& jointWorldPositions() const { return jointWorldPos; }
		const std::vector<int>&       jointParents()        const { return parentsCache; } // parent index per joint (-1 = root)
		float         handleScale()           const { return gizmoScale; } // world size of the handles

	private:
		entt::registry& registry;

		// mode / selection
		bool         editMode = false;
		GizmoMode    gmode    = GizmoMode::Rotate;
		entt::entity target   = entt::null;
		int          selJoint = -1;
		int          hoverAxis = -1;
		int          dragAxis  = -1;
		bool         localSpace = false; // L toggles: false = world/global axes, true = bone-local

		// cached this frame
		glm::vec3              selJointWorldPos{ 0.0f };
		std::vector<glm::vec3> jointWorldPos;
		std::vector<int>       parentsCache; // copy of skeleton.parents for the render system
		std::vector<glm::mat4> globals;     // joint local->model matrices
		glm::mat4              entityModel{ 1.0f };
		float                  gizmoScale = 1.0f;

		// drag anchors (captured on mouse-down, FIXED for the whole drag — never recomputed from
		// the bone we are moving, or the reference frame would chase the bone and oscillate).
		glm::vec3 dragStartCenter{ 0.0f };   // joint world pos at mouse-down = axis/ring origin
		glm::vec3 dragStartAxis{ 1.0f, 0.0f, 0.0f }; // world-space drag axis, frozen at mouse-down
		float     dragStartAxisCoord = 0.0f; // translate: signed distance along the axis
		glm::vec3 dragStartTrans{ 0.0f };
		glm::quat dragStartRot{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 dragStartHit{ 0.0f };      // rotate: world point where the drag began

		// input edge state
		bool keyGPrev = false, keyTPrev = false, keyRPrev = false, keyLPrev = false, mouseLeftPrev = false;

		// helpers (implemented in the .cpp)
		glm::vec3 axisDir(int i) const; // world dir of axis i (world axis, or bone-local in local space)
		void   refreshTarget();
		void   makeRay(GLFWwindow* window, const BGLCamera& camera, float vpW, float vpH,
		               glm::vec3& outOrigin, glm::vec3& outDir) const;
		int    pickJoint(const glm::vec3& o, const glm::vec3& d) const;
		int    pickAxis(const glm::vec3& o, const glm::vec3& d, glm::vec3& outHit, float& outAxisCoord) const;
		void   applyDrag(const glm::vec3& o, const glm::vec3& d);
	};

} // namespace bagel
