set(DEPS "${CMAKE_CURRENT_LIST_DIR}/Dependencies")

set(TINYGLTF_PATH       ${DEPS}/tinygltf)
set(TINYOBJ_PATH        ${DEPS}/tinyobjloader)
# GLFW on Windows is a precompiled binary release, not a clone, so its folder name
# carries the version: glfw-3.3.8.bin.WIN64, glfw-3.4.bin.WIN64, ... Take whichever is
# unzipped into Dependencies/ rather than hardcoding one version. On Linux/macOS there
# is no such binary drop — GLFW comes from find_package(glfw3) (system package), so this
# whole block is Windows-only and must not FATAL_ERROR elsewhere.
if (WIN32)
  file(GLOB GLFW_CANDIDATES "${DEPS}/glfw-*.bin.WIN64")
  if(NOT GLFW_CANDIDATES)
    message(FATAL_ERROR
      "GLFW not found. Download the 64-bit Windows binaries from "
      "https://www.glfw.org/download and unzip them into ${DEPS}/ "
      "(giving e.g. ${DEPS}/glfw-3.4.bin.WIN64).")
  endif()
  list(SORT GLFW_CANDIDATES)
  list(GET GLFW_CANDIDATES -1 GLFW_PATH) # newest version if several are present
endif()
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