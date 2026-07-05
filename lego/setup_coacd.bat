@echo off
setlocal
REM Creates a Python venv next to this script and installs CoACD into it, so the
REM collision baker (partsParser.exe --coacd) can shell out to coacd_decompose.py.
REM One-time setup; re-run to upgrade. Requires python on PATH.

set "HERE=%~dp0"
set "VENV=%HERE%.venv"

where python >nul 2>nul
if errorlevel 1 (
    echo ERROR: python not found on PATH.
    exit /b 1
)

if not exist "%VENV%\Scripts\python.exe" (
    echo === Creating venv at %VENV% ===
    python -m venv "%VENV%" || (echo [FAILED] venv create & exit /b 1)
)

echo === Installing coacd + numpy ===
"%VENV%\Scripts\python.exe" -m pip install --upgrade pip
"%VENV%\Scripts\python.exe" -m pip install coacd numpy || (echo [FAILED] pip install & exit /b 1)

echo [OK] CoACD venv ready: %VENV%
exit /b 0
