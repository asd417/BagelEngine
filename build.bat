@echo off
setlocal

set "GLSLC=C:\VulkanSDK\1.3.261.1\Bin\glslc.exe"
set "S=%~dp0shaders"
set /a ERRORS=0

echo.
echo === Compiling shaders ===

if not exist "%GLSLC%" (
    echo ERROR: glslc not found at "%GLSLC%"
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

"%GLSLC%" "%S%\deferred_lighting.vert"  -o "%S%\deferred_lighting.vert.spv"
if errorlevel 1 (echo [FAIL] deferred_lighting.vert  & set /a ERRORS+=1) else echo [OK] deferred_lighting.vert

"%GLSLC%" "%S%\deferred_lighting.frag"  -o "%S%\deferred_lighting.frag.spv"
if errorlevel 1 (echo [FAIL] deferred_lighting.frag  & set /a ERRORS+=1) else echo [OK] deferred_lighting.frag

"%GLSLC%" "%S%\transparent.frag"        -o "%S%\transparent.frag.spv"
if errorlevel 1 (echo [FAIL] transparent.frag        & set /a ERRORS+=1) else echo [OK] transparent.frag


"%GLSLC%" "%S%\radiosity.frag"           -o "%S%\radiosity.frag.spv"
if errorlevel 1 (echo [FAIL] radiosity.frag           & set /a ERRORS+=1) else echo [OK] radiosity.frag

"%GLSLC%" "%S%\bloom_downsample.frag"   -o "%S%\bloom_downsample.frag.spv"
if errorlevel 1 (echo [FAIL] bloom_downsample.frag   & set /a ERRORS+=1) else echo [OK] bloom_downsample.frag

"%GLSLC%" "%S%\bloom_upsample.frag"     -o "%S%\bloom_upsample.frag.spv"
if errorlevel 1 (echo [FAIL] bloom_upsample.frag     & set /a ERRORS+=1) else echo [OK] bloom_upsample.frag

if %ERRORS% gtr 0 (
    echo %ERRORS% shader^(s^) failed. Aborting.
    exit /b 1
)

echo.
echo === Configuring CMake project ===
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake not found on PATH.
    exit /b 1
)
REM CMakeLists.txt globs sources, so reconfigure to pick up added/moved/removed
REM files (e.g. src/model_loaders, src/map) before building.
cmake -S "%~dp0." -B "%~dp0build"
if errorlevel 1 (
    echo [FAILED] CMake configure
    exit /b 1
)
echo [OK] CMake configure

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"

if not defined MSBUILD (
    echo ERROR: MSBuild.exe not found via vswhere.
    exit /b 1
)

set "SLN=%~dp0build\BagelEngine.sln"
set PLATFORM=x64

echo.
echo === Debug ^| %PLATFORM% ===
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=%PLATFORM% /m /v:m /nologo
if errorlevel 1 (echo [FAILED] Debug & set /a ERRORS+=1) else echo [OK] Debug

echo.
echo === Release ^| %PLATFORM% ===
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=%PLATFORM% /m /v:m /nologo
if errorlevel 1 (echo [FAILED] Release & set /a ERRORS+=1) else echo [OK] Release

echo.
if %ERRORS% == 0 (
    echo All configurations built successfully.
    exit /b 0
) else (
    echo %ERRORS% configuration^(s^) failed.
    exit /b 1
)