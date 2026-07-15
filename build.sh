#!/usr/bin/env bash
# Linux/macOS counterpart to build.bat.
#
# Windows builds with clang-cl + vcvarsall (it targets the MSVC ABI and borrows MSVC's
# STL + Windows SDK). None of that applies here: we use the system compiler (clang++ if
# present, else the default) with the same Ninja Multi-Config layout, so one build/ dir
# holds both Debug and Release exactly like the Windows side.
#
# Prereqs the package manager / Vulkan SDK provide: cmake, ninja, glfw3 (dev package),
# the Vulkan loader + headers, and glslc. GLFW comes from find_package(glfw3) here, not
# the precompiled Windows binary drop (see .env.cmake / CMakeLists.txt UNIX branch).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# --- Vulkan SDK: glslc compiles the shaders to SPIR-V, same list the CMake Shaders
#     target would, but done up front so a shader error aborts before the C++ build. ---
if [[ -z "${VULKAN_SDK:-}" ]]; then
  echo "ERROR: VULKAN_SDK is not set. Install the Vulkan SDK and 'source setup-env.sh'"
  echo "       (https://vulkan.lunarg.com/sdk/home), or install the distro's vulkan dev packages."
  exit 1
fi

GLSLC="$VULKAN_SDK/bin/glslc"
if [[ ! -x "$GLSLC" ]]; then
  GLSLC="$(command -v glslc || true)"
fi
if [[ -z "$GLSLC" || ! -x "$GLSLC" ]]; then
  echo "ERROR: glslc not found (looked in \$VULKAN_SDK/bin and on PATH)."
  exit 1
fi

echo
echo "=== Compiling shaders ==="
shopt -s nullglob
shader_errors=0
for src in shaders/*.vert shaders/*.frag; do
  if "$GLSLC" "$src" -o "$src.spv"; then
    echo "[OK]   $(basename "$src")"
  else
    echo "[FAIL] $(basename "$src")"
    shader_errors=$((shader_errors + 1))
  fi
done
if [[ $shader_errors -ne 0 ]]; then
  echo "$shader_errors shader(s) failed. Aborting."
  exit 1
fi

echo
echo "=== Locating toolchain ==="
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found on PATH."; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "ERROR: ninja not found on PATH."; exit 1; }

# The author wants clang everywhere; fall back to the platform default (gcc) if it is absent.
cmake_args=(-S . -B build -G "Ninja Multi-Config" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
if command -v clang++ >/dev/null 2>&1 && command -v clang >/dev/null 2>&1; then
  cmake_args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
  echo "[OK] using clang / clang++"
else
  echo "[OK] using default compiler (clang not found)"
fi

echo
echo "=== Configuring CMake project ==="
# CMakeLists globs sources, so reconfigure to pick up added/moved/removed files.
cmake "${cmake_args[@]}"

echo
echo "=== Debug ==="
cmake --build build --config Debug

echo
echo "=== Release ==="
cmake --build build --config Release

echo
echo "All configurations built successfully."
