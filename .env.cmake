set(DEPS "${CMAKE_CURRENT_LIST_DIR}/Dependencies")

set(TINYGLTF_PATH       ${DEPS}/tinygltf)
set(TINYOBJ_PATH        ${DEPS}/tinyobjloader)
set(GLFW_PATH           ${DEPS}/glfw-3.3.8.bin.WIN64)
set(GLM_PATH            ${DEPS}/glm)
set(KTX                 ${DEPS}/KTX-Software/lib/include)
set(KTX_KHR             ${DEPS}/KTX-Software/external/dfdutils)
set(KTX_LIB             ${DEPS}/KTX-Software/build/lib)
set(STB                 ${DEPS}/stb)
set(ENTT                ${DEPS}/entt/single_include/entt)
set(VULKAN_SDK_PATH     $ENV{VULKAN_SDK})
set(IMGUI               ${DEPS}/imgui)
set(JOLT                ${DEPS}/JoltPhysics)
set(XATLAS              ${DEPS}/xatlas/source/xatlas)


# Set MINGW_PATH if using mingwBuild.bat and not VisualStudio20XX
# set(MINGW_PATH "C:/Program Files/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64")
# Optional set TINYOBJ_PATH to target specific version, otherwise defaults to external/tinyobjloader
# set(TINYOBJ_PATH X:/dev/Libraries/tinyobjloader)