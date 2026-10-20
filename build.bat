@echo off
setlocal

REM The Vulkan SDK is the one dependency the user installs themselves; everything else
REM is cloned and built by this repo. VULKAN_SDK is set by the SDK installer.
if not defined VULKAN_SDK (
    echo ERROR: VULKAN_SDK is not set. Install the Vulkan SDK from
    echo        https://vulkan.lunarg.com/sdk/home and reopen your terminal.
    exit /b 1
)
set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
set "S=%~dp0shaders"
set /a ERRORS=0

echo.
echo === Compiling shaders ===

if not exist "%GLSLC%" (
    echo ERROR: glslc not found at "%GLSLC%"
    echo        VULKAN_SDK is "%VULKAN_SDK%" - is the install complete?
    exit /b 1
)

"%GLSLC%" "%S%\wireframe_shader.vert"   -o "%S%\wireframe_shader.vert.spv"
if errorlevel 1 (echo [FAIL] wireframe_shader.vert   & set /a ERRORS+=1) else echo [OK] wireframe_shader.vert

"%GLSLC%" "%S%\wireframe_shader.frag"   -o "%S%\wireframe_shader.frag.spv"
if errorlevel 1 (echo [FAIL] wireframe_shader.frag   & set /a ERRORS+=1) else echo [OK] wireframe_shader.frag

"%GLSLC%" "%S%\gbuffer_fill.vert"       -o "%S%\gbuffer_fill.vert.spv"
if errorlevel 1 (echo [FAIL] gbuffer_fill.vert       & set /a ERRORS+=1) else echo [OK] gbuffer_fill.vert

"%GLSLC%" "%S%\gbuffer_fill.frag"       -o "%S%\gbuffer_fill.frag.spv"
if errorlevel 1 (echo [FAIL] gbuffer_fill.frag       & set /a ERRORS+=1) else echo [OK] gbuffer_fill.frag

"%GLSLC%" "%S%\planet_gbuffer.vert"     -o "%S%\planet_gbuffer.vert.spv"
if errorlevel 1 (echo [FAIL] planet_gbuffer.vert     & set /a ERRORS+=1) else echo [OK] planet_gbuffer.vert

"%GLSLC%" "%S%\planet_gbuffer.frag"     -o "%S%\planet_gbuffer.frag.spv"
if errorlevel 1 (echo [FAIL] planet_gbuffer.frag     & set /a ERRORS+=1) else echo [OK] planet_gbuffer.frag

"%GLSLC%" "%S%\deferred_lighting.vert"  -o "%S%\deferred_lighting.vert.spv"
if errorlevel 1 (echo [FAIL] deferred_lighting.vert  & set /a ERRORS+=1) else echo [OK] deferred_lighting.vert

"%GLSLC%" "%S%\deferred_lighting.frag"  -o "%S%\deferred_lighting.frag.spv"
if errorlevel 1 (echo [FAIL] deferred_lighting.frag  & set /a ERRORS+=1) else echo [OK] deferred_lighting.frag

"%GLSLC%" "%S%\transparent.frag"        -o "%S%\transparent.frag.spv"
if errorlevel 1 (echo [FAIL] transparent.frag        & set /a ERRORS+=1) else echo [OK] transparent.frag

"%GLSLC%" "%S%\water.vert"              -o "%S%\water.vert.spv"
if errorlevel 1 (echo [FAIL] water.vert              & set /a ERRORS+=1) else echo [OK] water.vert

"%GLSLC%" "%S%\water.frag"              -o "%S%\water.frag.spv"
if errorlevel 1 (echo [FAIL] water.frag              & set /a ERRORS+=1) else echo [OK] water.frag


"%GLSLC%" "%S%\radiosity.frag"           -o "%S%\radiosity.frag.spv"
if errorlevel 1 (echo [FAIL] radiosity.frag           & set /a ERRORS+=1) else echo [OK] radiosity.frag

"%GLSLC%" "%S%\bloom_downsample.frag"   -o "%S%\bloom_downsample.frag.spv"
if errorlevel 1 (echo [FAIL] bloom_downsample.frag   & set /a ERRORS+=1) else echo [OK] bloom_downsample.frag

"%GLSLC%" "%S%\bloom_upsample.frag"     -o "%S%\bloom_upsample.frag.spv"
if errorlevel 1 (echo [FAIL] bloom_upsample.frag     & set /a ERRORS+=1) else echo [OK] bloom_upsample.frag

"%GLSLC%" "%S%\smaa_edge.frag"     -o "%S%\smaa_edge.frag.spv"
if errorlevel 1 (echo [FAIL] smaa_edge.frag     & set /a ERRORS+=1) else echo [OK] smaa_edge.frag


if %ERRORS% gtr 0 (
    echo %ERRORS% shader^(s^) failed. Aborting.
    exit /b 1
)

echo.
echo === Locating Windows SDK and MSVC standard library ===
REM We compile with clang-cl, not cl.exe. But clang-cl targets the MSVC ABI and
REM ships neither a C++ standard library nor the Windows headers: it reads MSVC's
REM STL and the Windows SDK, and lld-link resolves against their .lib files.
REM vcvarsall exports the INCLUDE and LIB paths that make those findable.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    exit /b 1
)

REM -requires ...VC.Llvm.Clang, NOT a bare -latest: with several VS versions installed, the
REM NEWEST one is not necessarily the one carrying the clang compiler. Two ways that bites:
REM   * the newest install may ship only clang-format/clang-tidy (no clang-cl at all), and
REM   * its MSVC STL hard-errors on an older Clang ("STL1000: expected Clang 20 or newer"),
REM     so pairing the new STL with an older install's clang-cl cannot work either.
REM Selecting the install that HAS clang keeps its STL and its clang-cl matched by construction.
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo ERROR: No Visual Studio install with the "C++ Clang Compiler for Windows"
    echo        component was found. Add it via the Visual Studio Installer
    echo        ^(Individual components -^> C++ Clang Compiler for Windows^).
    exit /b 1
)

set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvarsall.bat not found at "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" x64 >nul
if errorlevel 1 (
    echo [FAILED] vcvarsall
    exit /b 1
)
REM vcvarsall does NOT put the LLVM tools on PATH, so do it here rather than depending on the
REM user's PATH — that is what broke when a newer VS install shadowed the one holding clang-cl.
set "LLVMBIN=%VSPATH%\VC\Tools\Llvm\x64\bin"
set "PATH=%LLVMBIN%;%PATH%"
if not exist "%LLVMBIN%\clang-cl.exe" (
    echo ERROR: clang-cl.exe not found in "%LLVMBIN%"
    exit /b 1
)
echo [OK] Windows SDK + MSVC stdlib on PATH ^(x64^); compiler is clang-cl

echo.
echo === Configuring CMake project ===
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake not found on PATH.
    exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo ERROR: ninja not found on PATH.
    exit /b 1
)
REM CMakeLists.txt globs sources, so reconfigure to pick up added/moved/removed
REM files (e.g. src/model_loaders, src/map) before building.
REM Ninja Multi-Config keeps one build dir holding both Debug and Release, the way
REM the old .sln did. It also emits compile_commands.json, which the VS Code C/C++
REM extension and clang-tidy read so editor diagnostics match the compiler exactly.
REM CMAKE_LINKER and CMAKE_AR are pinned to the LLVM tools. Left to its own devices CMake
REM searches PATH and, on a machine with msys2/mingw installed, picks the MinGW `ld.exe` and
REM `ar.exe` — neither understands MSVC-style arguments, so links die with "cannot find
REM /nologo" and static libs with "ar: invalid option -- /". Both values also STICK in
REM build/CMakeCache.txt once written, so a bad pick survives until the cache is deleted.
REM Both are FULL paths on purpose: CMake stores them as FILEPATH cache entries and resolves a
REM bare tool name against the BUILD directory, not PATH ("'...\build\llvm-lib' is not
REM recognized"), so `-DCMAKE_AR=llvm-lib` silently produces a broken archiver.
cmake -S "%~dp0." -B "%~dp0build" -G "Ninja Multi-Config" ^
    -DCMAKE_C_COMPILER=clang-cl ^
    -DCMAKE_CXX_COMPILER=clang-cl ^
    "-DCMAKE_LINKER=%LLVMBIN%\lld-link.exe" ^
    "-DCMAKE_AR=%LLVMBIN%\llvm-lib.exe" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 (
    echo [FAILED] CMake configure
    exit /b 1
)
echo [OK] CMake configure

echo.
echo === Debug ===
cmake --build "%~dp0build" --config Debug
if errorlevel 1 (echo [FAILED] Debug & set /a ERRORS+=1) else echo [OK] Debug

echo.
echo === Release ===
cmake --build "%~dp0build" --config Release
if errorlevel 1 (echo [FAILED] Release & set /a ERRORS+=1) else echo [OK] Release

echo.
if %ERRORS% == 0 (
    echo All configurations built successfully.
    exit /b 0
) else (
    echo %ERRORS% configuration^(s^) failed.
    exit /b 1
)