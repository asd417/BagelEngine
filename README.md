# Bagel

Game engine using Vulkan

See Wiki for devlog

## Building

Install these yourself:

- Visual Studio 2022, with these components:
  - C++ Clang Compiler for Windows
  - C++ Clang-cl for v143 build tools
  - C++ CMake tools for Windows
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
- [GLFW](https://www.glfw.org/download), the precompiled 64-bit Windows binaries

The Vulkan SDK installer sets `VULKAN_SDK` machine-wide, so there is nothing to
configure by hand. Reopen your terminal afterwards so it picks the variable up.

GLFW is shipped as a precompiled binary rather than source, so the setup script cannot
clone it. Unzip the download into `Dependencies/`, giving a folder such as:

```
Dependencies/glfw-3.4.bin.WIN64/
```

Any `glfw-*.bin.WIN64` folder is picked up automatically; there is nothing to edit.

CMake and Ninja need no separate install. Both ship with the CMake tools component,
and `build.bat` puts them on `PATH` via `vcvarsall.bat`.

Then, from a fresh clone:

```
setup.bat
```

That fetches every dependency (glm, tinyobjloader, stb, entt, imgui, tinygltf,
yaml-cpp, Jolt, KTX, xatlas), compiles the shaders, and builds Debug and Release.
The engine lands at `build/Release/BagelEngine.exe`.

Afterwards, `build.bat` rebuilds without re-fetching. `lib-setup.bat` fetches
dependencies on its own; it is safe to re-run and skips anything already present.

`Dependencies/` is gitignored and starts out empty. Everything in it is a plain clone
pinned to an exact commit in `lib-setup.bat`. There are no git submodules, and nothing
in there is committed, so the pins are the only record of which versions we build.

Jolt, KTX and yaml-cpp are compiled from source with the same compiler as the engine
(Ninja plus clang-cl). imgui and xatlas are compiled directly into the executable.
glm, stb, entt, tinygltf and tinyobjloader are header-only. GLFW is the sole exception:
a prebuilt `glfw3.lib`, which is why you download it by hand.