@echo off
REM One-shot setup for a fresh clone: fetch every dependency, then build the engine.
REM
REM Prerequisites you must install yourself:
REM   - Visual Studio 2022 with "C++ Clang Compiler for Windows" and
REM     "MSBuild support for LLVM (clang-cl) toolset"
REM   - Vulkan SDK          https://vulkan.lunarg.com/sdk/home  (sets VULKAN_SDK)
REM   - CMake and Ninja on PATH
REM
REM Everything else (Jolt, KTX, yaml-cpp, imgui, entt, stb, tinygltf, xatlas) is
REM fetched and compiled from source by these scripts.

call "%~dp0lib-setup.bat"
if errorlevel 1 (
    echo.
    echo Dependency fetch failed. Aborting.
    exit /b 1
)

call "%~dp0build.bat"
if errorlevel 1 (
    echo.
    echo Build failed.
    exit /b 1
)

echo.
echo Setup complete. Run build\Release\BagelEngine.exe
exit /b 0
