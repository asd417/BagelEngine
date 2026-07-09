@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Fetch every dependency as a plain clone pinned to an exact commit.
REM
REM No git submodules: Dependencies/ is gitignored and each library is cloned here,
REM pinned by SHA. Reproducibility comes from the pins below, not from a gitlink.
REM Pinning by SHA rather than by tag matters: every one of these is ahead of its
REM last release tag, so `--branch <tag>` would silently move the code backwards.
REM
REM Safe to re-run: a dependency already at its pinned SHA is skipped.
REM
REM Not fetched here:
REM   - GLFW ships as a precompiled binary release, so it cannot be cloned. Download
REM     it from https://www.glfw.org/download and unzip into Dependencies/, then point
REM     GLFW_PATH in .env.cmake at the folder.
REM   - the Vulkan SDK is a user install, from https://vulkan.lunarg.com/sdk/home

set "DEPS=%~dp0Dependencies"
set /a ERRORS=0

where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: git not found on PATH.
    exit /b 1
)

echo.
echo === Fetching dependencies ===

REM These two were vendored in-tree, so both are pinned to the exact commit that was
REM checked in. The old script named glm 1.0.3 and tinyobjloader v1.0.6, but the tree
REM actually held glm 0.9.9.8 and tinyobjloader 2.0.0. v1.0.6 has no tinyobj::ObjReader,
REM which src/model/model_loaders/obj.cpp depends on.
call :fetch glm           https://github.com/g-truc/glm.git                  bf71a834948186f4097caa076cd2663c69a10e1e
call :fetch tinyobjloader https://github.com/tinyobjloader/tinyobjloader.git 853f059d778058a43c954850e561a231934b33a7
call :fetch stb          https://github.com/nothings/stb.git              31c1ad37456438565541f4919958214b6e762fb4
call :fetch entt         https://github.com/skypjack/entt.git             1333fa53129e7cfded5a9640c4336a254049917b
call :fetch imgui        https://github.com/ocornut/imgui.git             a23e9fb1b53c43139d9ae1d1b85253ae41353cad
call :fetch tinygltf     https://github.com/syoyo/tinygltf.git            d31c16e333a6c8d593cad43f325f4e1825dd4776
call :fetch yaml-cpp     https://github.com/jbeder/yaml-cpp.git           f7320141120f720aecc4c32be25586e7da9eb978
call :fetch JoltPhysics  https://github.com/jrouwe/JoltPhysics.git        f458a60722d106c0a6566f008a6e25ddd8002dd9
call :fetch KTX-Software https://github.com/KhronosGroup/KTX-Software.git b0e5077581382bd6e92c191a5082ce7822acb2f9
call :fetch xatlas       https://github.com/jpcy/xatlas.git               f700c7790aaa030e794b52ba7791a05c085faf0c

echo.
if %ERRORS% gtr 0 (
    echo %ERRORS% dependency^(ies^) failed.
    exit /b 1
)
echo All dependencies ready.
exit /b 0

REM ---- :fetch <name> <url> <sha> -------------------------------------------
REM Clones shallowly at exactly <sha>. GitHub serves fetch-by-SHA, so there is no
REM need to download history just to reach the commit we want.
:fetch
set "NAME=%~1"
set "URL=%~2"
set "SHA=%~3"
set "DIR=%DEPS%\%NAME%"

REM Kept out of a parenthesized block: a for /f capture with a redirect behaves
REM differently inside one, and silently leaves HEAD unset.
set "HEAD="
if exist "%DIR%\.git" for /f "delims=" %%h in ('git -C "%DIR%" rev-parse HEAD') do set "HEAD=%%h"

if /i "%HEAD%"=="%SHA%" (
    echo [OK]   %NAME% ^(already at %SHA:~0,7%^)
    exit /b 0
)

if exist "%DIR%\.git" (
    echo [MOVE] %NAME% -^> %SHA:~0,7%
) else (
    echo [CLONE] %NAME%
    git init --quiet "%DIR%"                     || (call :failed %NAME% & exit /b 0)
    git -C "%DIR%" remote add origin "%URL%"     || (call :failed %NAME% & exit /b 0)
)

REM --force: a dependency may already exist as loose vendored files (no .git), and a
REM plain checkout would refuse to overwrite them.
git -C "%DIR%" fetch --quiet --depth 1 origin %SHA%           || (call :failed %NAME% & exit /b 0)
git -C "%DIR%" checkout --quiet --force --detach FETCH_HEAD   || (call :failed %NAME% & exit /b 0)
echo [OK]   %NAME% ^(%SHA:~0,7%^)
exit /b 0

:failed
echo [FAILED] %~1
set /a ERRORS+=1
exit /b 0
