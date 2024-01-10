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
 
 
# 2. Set GLFW_PATH in .env.cmake to target specific glfw
if (DEFINED GLFW_PATH)
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
 
# file(GLOB_RECURSE SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/src/*.h")

file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/src/*.h")

file(GLOB RENDERSYSTEM 	"${PROJECT_SOURCE_DIR}/src/render_systems/*.cpp" "${PROJECT_SOURCE_DIR}/src/render_systems/*.hpp")
file(GLOB PHYSICS 	"${PROJECT_SOURCE_DIR}/src/physics/*.cpp" "${PROJECT_SOURCE_DIR}/src/physics/*.hpp")
file(GLOB MATH		"${PROJECT_SOURCE_DIR}/src/math/*.cpp" "${PROJECT_SOURCE_DIR}/src/math/*.hpp")

file(GLOB IMGUI_SRC "${IMGUI}/*.cpp" "${IMGUI}/*.h")
file(GLOB IMGUI_BCKEND_vk ${IMGUI}/backends/imgui_impl_vulkan.*)
file(GLOB IMGUI_BCKEND_glfw ${IMGUI}/backends/imgui_impl_glfw.*)

file(GLOB TINYGLTF "${TINYGLTF_PATH}/tiny_gltf.h")

add_executable(${PROJECT_NAME} 
	${SOURCES} 
	${RENDERSYSTEM}
	${PHYSICS}
	${MATH}
	${IMGUI_SRC} 
	${IMGUI_BCKEND_vk} 
	${IMGUI_BCKEND_glfw}
	${TINYGLTF})

source_group("Render Systems"	FILES ${RENDERSYSTEM})
source_group("Physics"		FILES ${PHYSICS})
source_group("Math"		FILES ${MATH})
source_group("Imgui"            FILES ${IMGUI_SRC} ${IMGUI_BCKEND_vk} ${IMGUI_BCKEND_glfw})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
 
set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

target_compile_definitions(BagelEngine PUBLIC "$<$<CONFIG:Debug>:_DEBUG;JPH_PROFILE_ENABLED;JPH_DEBUG_RENDERER>")
target_compile_definitions(BagelEngine PUBLIC "$<$<CONFIG:Release>:NDEBUG;JPH_PROFILE_ENABLED;JPH_DEBUG_RENDERER>")

target_compile_definitions(BagelEngine PUBLIC "$<$<CONFIG:Debug>:JPH_FLOATING_POINT_EXCEPTIONS_ENABLED>")
target_compile_definitions(BagelEngine PUBLIC "$<$<CONFIG:Release>:JPH_FLOATING_POINT_EXCEPTIONS_ENABLED>")


if (WIN32)
  message(STATUS "CREATING BUILD FOR WINDOWS")
 
  target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${TINYOBJ_PATH}
    ${TINYGLTF_PATH}
    ${GLFW_INCLUDE_DIRS}
    ${GLM_PATH}
    ${KTX}
    ${STB}
    ${ENTT}
    ${IMGUI}
    ${JOLT}
    )
	
  target_include_directories(${PROJECT_NAME} PUBLIC ${JoltPhysics_SOURCE_DIR}/..)
 
  target_link_directories(${PROJECT_NAME} PUBLIC 
	${Vulkan_LIBRARIES} 
	${GLFW_LIB} 
	$<$<CONFIG:Debug>:${KTX_LIB}/Debug> 
	$<$<CONFIG:Debug>:${JOLT}/Build/VS2022_CL/Debug>)
  
  target_link_directories(${PROJECT_NAME} PUBLIC 
	${Vulkan_LIBRARIES} 
	${GLFW_LIB} 
	$<$<CONFIG:Release>:${KTX_LIB}/Release> 
	$<$<CONFIG:Release>:${JOLT}/Build/VS2022_CL/Release>)

  target_link_libraries(${PROJECT_NAME} glfw3 vulkan-1 ktx Jolt)

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