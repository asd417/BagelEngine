@echo off
setlocal
REM Builds the standalone LDraw parts tool -- connector baker / thumbnail generator
REM (partsParser.exe). Sources live in src\lego\partsParser\.
REM
REM The tool depends only on the engine-agnostic ldraw parser (glm + std) plus
REM stb_image_write for PNG thumbnails, so it builds with a plain g++ invocation --
REM no MSBuild / engine link required. ldraw_library.cpp still lives in src\lego\.
REM
REM Run from anywhere:   lego\build_bake_connectors.bat
REM Then run the tool:   lego\partsParser.exe
REM   (bakes every parts\*.dat into lego\baked\connectors.bin, plus thumbnails and
REM    collision hulls by default; pass part names to bake a subset, or --root / --out
REM    / --thumb-dir / --collision-dir / --no-* to override. See bake_connectors.cpp.)

REM This script now lives in the engine's lego\ dir; the engine root is one level up.
REM Sources live in src\lego\partsParser\ (and src\lego\ for the shared parser). The
REM built binary goes to lego\partsParser.exe (outputs default to lego\baked\).
set "ROOT=%~dp0.."
set "SRC=%ROOT%\src\lego\partsParser"
set "OUT=%ROOT%\lego\partsParser.exe"

where g++ >nul 2>nul
if errorlevel 1 (
    echo ERROR: g++ not found on PATH.
    exit /b 1
)

echo.
echo === Building connector baker / thumbnail / collision generator ===
g++ -std=c++17 -O2 -I "%ROOT%\src\lego" -I "%ROOT%\Dependencies\glm" -I "%ROOT%\Dependencies\stb" "%SRC%\bake_connectors.cpp" "%ROOT%\src\lego\ldraw_library.cpp" -o "%OUT%"
if errorlevel 1 (
    echo [FAILED] g++ build
    exit /b 1
)

echo [OK] %OUT%
exit /b 0
