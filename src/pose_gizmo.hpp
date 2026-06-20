#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <string>

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
		int           selectedJoint()         const { return selJoint; }          // active/primary member
		const std::vector<int>& selectedJoints() const { return selJoints; }      // full multi-selection set
		const char*   selectedJointName()     const { return selJointName.c_str(); } // "" if none/unnamed
		GizmoMode     mode()                  const { return gmode; }
		int           hoveredAxis()           const { return hoverAxis; }  // -1 none, 0/1/2 = X/Y/Z
		int           draggingAxis()          const { return dragAxis; }   // -1 none
		bool          localSpaceOn()          const { return localSpace; } // true = bone-local axes
		// Orientation the handles are drawn with: identity in global space, the selected bone's
		// orthonormal rotation in local space.
		glm::mat4     handleBasis()           const;
		glm::vec3     selectedJointWorldPos()  const { return selCenter; } // batch pivot (centroid of selection)
		// Full world matrix of the selected joint (its basis columns are the bone's local axes).
		glm::mat4     selectedJointWorldMatrix() const {
			return (selJoint >= 0 && selJoint < static_cast<int>(globals.size()))
				? entityModel * globals[selJoint] : glm::mat4{ 1.0f };
		}
		const std::vector<glm::vec3>& jointWorldPositions() const { return jointWorldPos; }
		const std::vector<glm::mat4>& jointWorldMatrices()  const { return jointWorldMat; } // per joint, for orientation
		const std::vector<int>&       jointParents()        const { return parentsCache; } // parent index per joint (-1 = root)
		float         handleScale()           const { return gizmoScale; } // world size of the handles

	private:
		entt::registry& registry;

		// mode / selection
		bool         editMode = false;
		GizmoMode    gmode    = GizmoMode::Rotate;
		entt::entity target   = entt::null;
		int          selJoint = -1;          // active/primary joint (handle basis, name display)
		std::vector<int> selJoints;          // full selection set (Shift+click toggles membership)
		int          hoverAxis = -1;
		int          dragAxis  = -1;
		bool         localSpace = false; // L toggles: false = world/global axes, true = bone-local

		// cached this frame
		glm::vec3              selJointWorldPos{ 0.0f }; // active joint world pos
		glm::vec3              selCenter{ 0.0f };        // centroid of selJoints = the batch pivot/handle origin
		std::string            selJointName;        // name of the selected joint (from skeleton.names)
		std::vector<glm::vec3> jointWorldPos;
		std::vector<glm::mat4> jointWorldMat; // entityModel * globals[j], for per-joint orientation
		std::vector<int>       parentsCache; // copy of skeleton.parents for the render system
		std::vector<glm::mat4> globals;     // joint local->model matrices
		glm::mat4              entityModel{ 1.0f };
		float                  gizmoScale = 1.0f;

		// drag anchors (captured on mouse-down, FIXED for the whole drag — never recomputed from
		// the bone we are moving, or the reference frame would chase the bone and oscillate).
		glm::vec3 dragStartCenter{ 0.0f };   // joint world pos at mouse-down = axis/ring origin
		glm::vec3 dragStartAxis{ 1.0f, 0.0f, 0.0f }; // world-space drag axis, frozen at mouse-down
		float     dragStartAxisCoord = 0.0f; // translate: signed distance along the axis
		glm::vec3 dragStartHit{ 0.0f };      // rotate: world point where the drag began

		// Per-joint snapshot captured at mouse-down, one entry per "top-level" selected joint (a
		// selected joint with no selected ancestor — descendants follow rigidly via the hierarchy,
		// so transforming them too would double-move). Drag math rebuilds each joint's local TRS
		// from these fixed world anchors, so the batch transform never drifts.
		struct DragJoint {
			int       joint = -1;
			glm::mat4 parentWorldInv{ 1.0f };               // world->parent space, to write back local TRS
			glm::quat parentRot{ 1.0f, 0.0f, 0.0f, 0.0f };  // parent world rotation
			glm::vec3 startWorldPos{ 0.0f };
			glm::quat startWorldRot{ 1.0f, 0.0f, 0.0f, 0.0f };
		};
		std::vector<DragJoint> dragJoints;

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
		// multi-selection helpers
		bool   isSelected(int j) const;        // membership in selJoints
		bool   ancestorSelected(int j) const;  // any parent up the chain is in selJoints
		void   selectSingle(int j);            // replace selection with {j}
		void   toggleSelect(int j);            // add/remove j (Shift+click)
		void   buildDragJoints();              // snapshot top-level selected joints at mouse-down
	};

} // namespace bagel
