#!/bin/bash
set -e

# Fetch every dependency as a plain clone pinned to an exact commit.
#
# No git submodules: Dependencies/ is gitignored and each library is cloned here,
# pinned by SHA. Reproducibility comes from the pins below, not from a gitlink.
# Pinning by SHA rather than by tag matters — every one of these is ahead of its
# last release tag, so `--branch <tag>` would silently move the code backwards.
#
# Safe to re-run: a dependency already at its pinned SHA is skipped.
#
# Not fetched here:
#   - GLFW ships as a precompiled binary release, so it cannot be cloned. Download
#     it from https://www.glfw.org/download and unzip into Dependencies/, then point
#     GLFW_PATH in .env.cmake at the folder.
#   - the Vulkan SDK is a user install, from https://vulkan.lunarg.com/sdk/home

cd "$(dirname "$0")"
DEPS="$PWD/Dependencies"

command -v git >/dev/null 2>&1 || { echo "ERROR: git not found on PATH."; exit 1; }

# fetch_pinned <name> <url> <sha>
# Clones shallowly at exactly <sha>. GitHub serves fetch-by-SHA, so there is no
# need to download history just to reach the commit we want.
fetch_pinned() {
    local name="$1" url="$2" sha="$3"
    local dir="$DEPS/$name"

    if [ -e "$dir/.git" ]; then
        if [ "$(git -C "$dir" rev-parse HEAD)" = "$sha" ]; then
            echo "[OK]   $name (already at ${sha:0:7})"
            return 0
        fi
        echo "[MOVE] $name -> ${sha:0:7}"
    else
        echo "[CLONE] $name"
        git init --quiet "$dir"
        git -C "$dir" remote add origin "$url"
    fi

    # --force: a dependency may already exist as loose vendored files (no .git), and a
    # plain checkout would refuse to overwrite them.
    git -C "$dir" fetch --quiet --depth 1 origin "$sha"
    git -C "$dir" checkout --quiet --force --detach FETCH_HEAD
    echo "[OK]   $name (${sha:0:7})"
}

echo
echo "=== Fetching dependencies ==="

# These two were vendored in-tree, so both are pinned to the exact commit that was
# checked in. The old script named glm 1.0.3 and tinyobjloader v1.0.6, but the tree
# actually held glm 0.9.9.8 and tinyobjloader 2.0.0 — v1.0.6 has no tinyobj::ObjReader,
# which src/model/model_loaders/obj.cpp depends on.
fetch_pinned glm           https://github.com/g-truc/glm.git                  bf71a834948186f4097caa076cd2663c69a10e1e
fetch_pinned tinyobjloader https://github.com/tinyobjloader/tinyobjloader.git 853f059d778058a43c954850e561a231934b33a7
fetch_pinned stb          https://github.com/nothings/stb.git              31c1ad37456438565541f4919958214b6e762fb4
fetch_pinned entt         https://github.com/skypjack/entt.git             1333fa53129e7cfded5a9640c4336a254049917b
fetch_pinned imgui        https://github.com/ocornut/imgui.git             a23e9fb1b53c43139d9ae1d1b85253ae41353cad
fetch_pinned tinygltf     https://github.com/syoyo/tinygltf.git            d31c16e333a6c8d593cad43f325f4e1825dd4776
fetch_pinned yaml-cpp     https://github.com/jbeder/yaml-cpp.git           f7320141120f720aecc4c32be25586e7da9eb978
fetch_pinned JoltPhysics  https://github.com/jrouwe/JoltPhysics.git        f458a60722d106c0a6566f008a6e25ddd8002dd9
fetch_pinned KTX-Software https://github.com/KhronosGroup/KTX-Software.git b0e5077581382bd6e92c191a5082ce7822acb2f9
fetch_pinned xatlas       https://github.com/jpcy/xatlas.git               f700c7790aaa030e794b52ba7791a05c085faf0c

echo
echo "All dependencies ready."
