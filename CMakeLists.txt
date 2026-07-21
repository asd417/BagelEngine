include(./.env.cmake OPTIONAL RESULT_VARIABLE LOCAL_ENV)
message(STATUS "Local .env.cmake: ${LOCAL_ENV}")

cmake_minimum_required(VERSION 3.11.0)

set(CMAKE_SUPPRESS_REGENERATION true)
 
set(NAME BagelEngine)

message(STATUS "using ${CMAKE_GENERATOR}")
if (CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
    if (NOT MINGW_PATH) 
      message(FATAL_ERROR "MINGW_PATH not set in .env.cmake")
    endif()
    set(USE_MINGW "True")
    set(CMAKE_C_COMPILER ${MINGW_PATH}/bin/gcc.exe)
    set(CMAKE_CXX_COMPILER  ${MINGW_PATH}/bin/g++.exe)
endif()

 
project(${NAME} VERSION 0.00.1)
 
# 1. Set VULKAN_SDK_PATH in .env.cmake to target specific vulkan version
if (DEFINED VULKAN_SDK_PATH)
  set(Vulkan_INCLUDE_DIRS "${VULKAN_SDK_PATH}/Include") # 1.1 Make sure this include path is correct
  set(Vulkan_LIBRARIES "${VULKAN_SDK_PATH}/Lib") # 1.2 Make sure lib path is correct
  set(Vulkan_FOUND "True")
else()
  find_package(Vulkan REQUIRED) # throws error if could not find Vulkan
  message(STATUS "Found Vulkan: $ENV{VULKAN_SDK}")
endif()
if (NOT Vulkan_FOUND)
    message(FATAL_ERROR "Could not find Vulkan library!")
else()
    message(STATUS "Using vulkan lib at: ${Vulkan_LIBRARIES}")
endif()
 
 
# 2. Set GLFW_PATH in .env.cmake to target specific glfw (Windows only)
if (DEFINED GLFW_PATH AND NOT UNIX)
  message(STATUS "Using GLFW path specified in .env")
  set(GLFW_INCLUDE_DIRS "${GLFW_PATH}/include")
  if (MSVC)
    set(GLFW_LIB "${GLFW_PATH}/lib-vc2022") # 2.1 Update lib-vc2019 to use same version as your visual studio
  elseif (CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
    message(STATUS "USING MINGW")
    set(GLFW_LIB "${GLFW_PATH}/lib-mingw-w64") # 2.1 make sure matches glfw mingw subdirectory
  endif()
else()
  find_package(glfw3 3.3 REQUIRED)
  set(GLFW_LIB glfw)
  message(STATUS "Found GLFW")
endif()
if (NOT GLFW_LIB)
    message(FATAL_ERROR "Could not find glfw library!")
else()
    message(STATUS "Using glfw lib at: ${GLFW_LIB}")
endif()
 
include_directories(Dependencies)

# yaml-cpp (model .yaml sidecar parsing) — built from the submodule. Skip its tests/tools.
# yaml-cpp 0.8.0 declares cmake_minimum_required < 3.5, which CMake 4.x rejects; raise the floor.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(YAML_CPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL       OFF CACHE BOOL "" FORCE)
add_subdirectory(Dependencies/yaml-cpp)

# Jolt is built from source with our compiler and config, the same way yaml-cpp is.
# A prebuilt Jolt.lib was the source of two problems: a fresh clone had no way to
# produce it, and it silently kept whatever toolchain built it months ago (its /GL
# objects are unreadable by lld-link). Jolt disables its own samples/tests when it
# is not the root project, and it propagates the JPH_* defines as PUBLIC, so linking
# the target is enough — we no longer declare them by hand.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE) # engine + glfw3.lib are /MD
set(INTERPROCEDURAL_OPTIMIZATION    OFF CACHE BOOL "" FORCE) # plain native objects for lld-link
set(ENABLE_ALL_WARNINGS             OFF CACHE BOOL "" FORCE) # Jolt's warnings-as-errors, not ours
set(ENABLE_INSTALL                  OFF CACHE BOOL "" FORCE)
# Jolt's OVERRIDE_CXX_FLAGS replaces CMAKE_CXX_FLAGS_<CONFIG> wholesale, which drops the
# /MD that CMake puts there — leaving clang-cl to default to the static CRT and fail the
# link with "mismatch detected for 'RuntimeLibrary'". Keep the platform defaults instead.
set(OVERRIDE_CXX_FLAGS              OFF CACHE BOOL "" FORCE)
add_subdirectory(Dependencies/JoltPhysics/Build)

# KTX (.ktx2 texture loading), also built from source rather than linked as a prebuilt
# ktx.lib out of a hand-made KTX-Software/build/ tree that no script in this repo created.
# Everything except the library itself is turned off: the tools, tests, docs, and the JNI
# and Python bindings all pull in extra toolchains we do not need. KTX_FEATURE_TESTS
# defaults ON and would require the tests/cts submodule.
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_DOC   OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_JNI   OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_PY    OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_JS    OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) # static ktx; no DLL to copy next to the exe
add_subdirectory(Dependencies/KTX-Software)

# file(GLOB_RECURSE SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/src/*.h")

file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/src/*.h")
file(GLOB ENGINE 	"${PROJECT_SOURCE_DIR}/src/engine/*.cpp" "${PROJECT_SOURCE_DIR}/src/engine/*.hpp")
# These globs are deliberately per-directory, not GLOB_RECURSE: src/lego/.tmp holds
# scratch programs with their own main() that must never enter the build.
file(GLOB ENGINE_RENDERER "${PROJECT_SOURCE_DIR}/src/engine/renderer/*.cpp" "${PROJECT_SOURCE_DIR}/src/engine/renderer/*.hpp")
file(GLOB COMPUTE_SYSTEMS "${PROJECT_SOURCE_DIR}/src/compute_systems/*.cpp" "${PROJECT_SOURCE_DIR}/src/compute_systems/*.hpp")
file(GLOB RENDERSYSTEM 	"${PROJECT_SOURCE_DIR}/src/render_systems/*.cpp" "${PROJECT_SOURCE_DIR}/src/render_systems/*.hpp")
file(GLOB RENDERSYSTEM_THREADED "${PROJECT_SOURCE_DIR}/src/render_systems/threaded/*.cpp" "${PROJECT_SOURCE_DIR}/src/render_systems/threaded/*.hpp")
file(GLOB RENDERSYSTEM_NEW "${PROJECT_SOURCE_DIR}/src/render_systems/new/*.cpp" "${PROJECT_SOURCE_DIR}/src/render_systems/new/*.hpp")
file(GLOB PHYSICS 	"${PROJECT_SOURCE_DIR}/src/physics/*.cpp" "${PROJECT_SOURCE_DIR}/src/physics/*.hpp")
file(GLOB MATH		"${PROJECT_SOURCE_DIR}/src/math/*.cpp" "${PROJECT_SOURCE_DIR}/src/math/*.hpp")
file(GLOB MODEL		"${PROJECT_SOURCE_DIR}/src/model/*.cpp" "${PROJECT_SOURCE_DIR}/src/model/*.hpp")
file(GLOB MODEL_LOADERS "${PROJECT_SOURCE_DIR}/src/model/model_loaders/*.cpp" "${PROJECT_SOURCE_DIR}/src/model/model_loaders/*.hpp")
file(GLOB TEXTURE "${PROJECT_SOURCE_DIR}/src/texture/*.cpp" "${PROJECT_SOURCE_DIR}/src/texture/*.hpp")
file(GLOB ANIMATION 	"${PROJECT_SOURCE_DIR}/src/animation/*.cpp" "${PROJECT_SOURCE_DIR}/src/animation/*.hpp")
file(GLOB SMAA		"${PROJECT_SOURCE_DIR}/src/smaa/*.cpp" "${PROJECT_SOURCE_DIR}/src/smaa/*.hpp")
file(GLOB ECS	"${PROJECT_SOURCE_DIR}/src/ecs/*.cpp" "${PROJECT_SOURCE_DIR}/src/ecs/*.hpp" "${PROJECT_SOURCE_DIR}/src/ecs/components/*.cpp" "${PROJECT_SOURCE_DIR}/src/ecs/components/*.hpp")
file(GLOB PLANET	"${PROJECT_SOURCE_DIR}/src/planet/*.cpp" "${PROJECT_SOURCE_DIR}/src/planet/*.hpp" "${PROJECT_SOURCE_DIR}/src/planet/components/*.cpp" "${PROJECT_SOURCE_DIR}/src/planet/components/*.hpp" "${PROJECT_SOURCE_DIR}/src/planet/noise_filters/*.cpp" "${PROJECT_SOURCE_DIR}/src/planet/noise_filters/*.hpp")
file(GLOB GAME		"${PROJECT_SOURCE_DIR}/src/game/*.cpp" "${PROJECT_SOURCE_DIR}/src/game/*.hpp")
file(GLOB MAP		"${PROJECT_SOURCE_DIR}/src/map/*.cpp" "${PROJECT_SOURCE_DIR}/src/map/*.hpp")
file(GLOB APPLICATION	"${PROJECT_SOURCE_DIR}/src/application/*.cpp" "${PROJECT_SOURCE_DIR}/src/application/*.hpp")
file(GLOB IMGUI_ENGINE		"${PROJECT_SOURCE_DIR}/src/imgui/*.cpp" "${PROJECT_SOURCE_DIR}/src/imgui/*.hpp" "${PROJECT_SOURCE_DIR}/src/imgui/components/*.cpp" "${PROJECT_SOURCE_DIR}/src/imgui/components/*.hpp")

file(GLOB IMGUI_SRC "${IMGUI}/*.cpp" "${IMGUI}/*.h")
file(GLOB IMGUI_BCKEND_vk ${IMGUI}/backends/imgui_impl_vulkan.*)
file(GLOB IMGUI_BCKEND_glfw ${IMGUI}/backends/imgui_impl_glfw.*)

file(GLOB TINYGLTF "${TINYGLTF_PATH}/tiny_gltf.h")

file(GLOB XATLAS_SRC "${XATLAS}/xatlas.cpp" "${XATLAS}/xatlas.h")

add_executable(${PROJECT_NAME}
	${SOURCES}
  ${ENGINE}
  ${ENGINE_RENDERER}
  ${COMPUTE_SYSTEMS}
	${RENDERSYSTEM}
	${RENDERSYSTEM_THREADED}
	${RENDERSYSTEM_NEW}
	${PHYSICS}
	${MATH}
	${MODEL}
	${MODEL_LOADERS}
  ${TEXTURE}
	${ANIMATION}
	${SMAA}
	${ECS}
	${PLANET}
	${GAME}
	${MAP}
	${APPLICATION}
  ${IMGUI_ENGINE}
	${IMGUI_SRC}
	${IMGUI_BCKEND_vk}
	${IMGUI_BCKEND_glfw}
	${TINYGLTF}
	${XATLAS_SRC})

# imgui and xatlas are vendored .cpp files compiled into this target, so the target's
# warning flags would otherwise apply to code we don't own. Marking the dependency
# include dirs SYSTEM only silences their headers, not these translation units.
# Source-level options are appended after target-level ones, so -w wins here.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(
    ${IMGUI_SRC} ${IMGUI_BCKEND_vk} ${IMGUI_BCKEND_glfw} ${XATLAS_SRC}
    PROPERTIES COMPILE_OPTIONS "-w")
endif()

source_group("SMAA"		FILES ${SMAA})
source_group("Engine" FILES ${ENGINE})
source_group("Render Systems"	FILES ${RENDERSYSTEM})
source_group("Physics"		FILES ${PHYSICS})
source_group("Math"		FILES ${MATH})
source_group("Model"		FILES ${MODEL})
source_group("Model Loaders"	FILES ${MODEL_LOADERS})
source_group("Texture"	FILES ${TEXTURE})
source_group("Animation"	FILES ${ANIMATION})
source_group("ECS"	FILES ${ECS})
source_group("Planet"	FILES ${PLANET})
source_group("Game"		FILES ${GAME})
source_group("Map"		FILES ${MAP})
source_group("Application"	FILES ${APPLICATION})
source_group("Imgui" FILES ${IMGUI_ENGINE})
source_group("Imgui src"            FILES ${IMGUI_SRC} ${IMGUI_BCKEND_vk} ${IMGUI_BCKEND_glfw})
source_group("xatlas"           FILES ${XATLAS_SRC})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
 
set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

# The JPH_* defines arrive from the Jolt target's PUBLIC interface; declaring them
# here too would let them drift out of sync with how Jolt was actually compiled.
target_compile_definitions(BagelEngine PUBLIC "$<$<CONFIG:Debug>:_DEBUG>")
target_compile_definitions(BagelEngine PUBLIC "$<$<CONFIG:Release>:NDEBUG>")

target_compile_definitions(BagelEngine PUBLIC ENGINE_BASE_PATH="${CMAKE_SOURCE_DIR}")

# MSVC is true for both cl.exe and clang-cl (both take the MSVC command line).
# /Z7 embeds debug info per .obj so parallel compiles never share a PDB file.
# The warning flags are PRIVATE: engine sources only, never Jolt/yaml-cpp/ktx.
if (MSVC)
  target_compile_options(BagelEngine PRIVATE $<$<CONFIG:Debug>:/Z7>)

  # These are warnings for now, not errors. The engine has ~20 pre-existing narrowing
  # sites; promote to -Werror= / /we once they are fixed (see the audit in git log).
  if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(BagelEngine PRIVATE
      /wd4819
      /w34267  # size_t -> smaller type, possible data loss
      /w34244  # numeric conversion, possible data loss (e.g. double -> float)
      /w34838  # narrowing conversion in brace initialization
      /w34996) # deprecated/unsafe CRT calls (sprintf, etc.)

  elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # clang-cl accepts MSVC warning numbers and silently ignores them, so the /w3
    # flags above would evaporate. These are the equivalent clang diagnostics.
    target_compile_options(BagelEngine PRIVATE
      -Wshorten-64-to-32        # C4267: size_t -> int
      -Wimplicit-int-conversion # C4244: int -> narrower int
      -Wimplicit-float-conversion # C4244: double -> float
      -Wc++11-narrowing         # C4838: narrowing in brace init
      -Wdeprecated-declarations) # C4996: sprintf and friends
  endif()
endif()


if (WIN32)
  message(STATUS "CREATING BUILD FOR WINDOWS")

  target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/src)

  # Third-party headers are SYSTEM includes so our -Werror diagnostics never fire
  # inside them. stb_image.h in particular is compiled into this target (tiny_gltf
  # includes it) and has narrowing conversions we neither own nor want to patch.
  target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
    ${Vulkan_INCLUDE_DIRS}
    ${TINYGLTF_PATH}
    ${TINYOBJ_PATH}
    ${GLFW_INCLUDE_DIRS}
    ${GLM_PATH}
    ${KTX}
    ${KTX_KHR}
    ${STB}
    ${ENTT}
    ${IMGUI}
    ${JOLT}
    ${XATLAS}
    )

  target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${JoltPhysics_SOURCE_DIR}/..)

  # Search paths for the libraries still named as bare strings below (glfw3, vulkan-1).
  # Jolt, ktx and yaml-cpp are absent because they are real CMake targets now
  # (add_subdirectory), so each carries its own location.
  target_link_directories(${PROJECT_NAME} PUBLIC
	${Vulkan_LIBRARIES}
	${GLFW_LIB})

  target_link_libraries(${PROJECT_NAME} glfw3 vulkan-1 ktx Jolt yaml-cpp)

  # glfw3.lib prebuilt with /MD; suppress conflicting MSVCRT defaultlib in Debug
  target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/NODEFAULTLIB:MSVCRT>)

elseif (UNIX)
  message(STATUS "CREATING BUILD FOR LINUX/macOS")

  # System GLFW (there is no precompiled binary drop off-Windows — see .env.cmake).
  find_package(glfw3 3.3 REQUIRED)

  target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/src)

  target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
    ${Vulkan_INCLUDE_DIRS}
    ${TINYGLTF_PATH}
    ${TINYOBJ_PATH}
    ${GLM_PATH}
    ${KTX}
    ${KTX_KHR}
    ${STB}
    ${ENTT}
    ${IMGUI}
    ${JOLT}
    ${XATLAS}
    )
  target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${JoltPhysics_SOURCE_DIR}/..)

  # ktx, Jolt and yaml-cpp are add_subdirectory targets now, not prebuilt libs: each
  # carries its own output location and include dirs, so linking the target is enough
  # (the old build/Release and Build/Linux link dirs no longer exist). Only Vulkan is
  # still a bare-named lib needing a search path.
  target_link_directories(${PROJECT_NAME} PUBLIC ${Vulkan_LIBRARIES})

  target_link_libraries(${PROJECT_NAME} glfw vulkan ktx Jolt yaml-cpp)

endif()

############## Build SHADERS #######################
 
# Find all vertex and fragment sources within shaders directory
# taken from VBlancos vulkan tutorial
# https://github.com/vblanco20-1/vulkan-guide/blob/all-chapters/CMakeLists.txt
find_program(GLSL_VALIDATOR glslangValidator HINTS 
  ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} 
  /usr/bin 
  /usr/local/bin 
  ${VULKAN_SDK_PATH}/Bin
  ${VULKAN_SDK_PATH}/Bin32
  $ENV{VULKAN_SDK}/Bin/ 
  $ENV{VULKAN_SDK}/Bin32/
)
 
# get all .vert and .frag files in shaders directory
file(GLOB_RECURSE GLSL_SOURCE_FILES
  "${PROJECT_SOURCE_DIR}/shaders/*.frag"
  "${PROJECT_SOURCE_DIR}/shaders/*.vert"
)
 
foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)
 
add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
)