# Downloads the Khronos/Intel Sponza (glTF) into models/sponza/
# Uses git sparse-checkout to avoid cloning the entire sample-assets repo.

$ErrorActionPreference = "Stop"

$repo     = "https://github.com/KhronosGroup/glTF-Sample-Assets.git"
$repoPath = "Models/Sponza/glTF"
$tmpDir   = "$PSScriptRoot\_sponza_tmp"
$outDir   = "$PSScriptRoot\models\sponza"

if (Test-Path $outDir) {
    Write-Host "models/sponza already exists — delete it first if you want a fresh download."
    exit 0
}

Write-Host "Cloning Sponza (sparse) from KhronosGroup/glTF-Sample-Assets..."
git clone --no-checkout --depth 1 --filter=blob:none $repo $tmpDir

Push-Location $tmpDir
try {
    git sparse-checkout init --cone
    git sparse-checkout set $repoPath
    git checkout
} finally {
    Pop-Location
}

$src = Join-Path $tmpDir $repoPath
if (-not (Test-Path $src)) {
    Write-Error "Sparse checkout succeeded but source path not found: $src"
    exit 1
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item -Recurse -Path "$src\*" -Destination $outDir

Remove-Item -Recurse -Force $tmpDir

Write-Host "Done. Sponza written to: $outDir"
Write-Host "Load it in-engine with: /models/sponza/Sponza.gltf"
