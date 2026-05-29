#!/bin/bash
set -e

cd "$(dirname "$0")/Dependencies"

clone_if_empty() {
    local dir="$1"
    local url="$2"
    local branch="$3"

    if [ -d "$dir" ] && [ -n "$(ls -A "$dir" 2>/dev/null)" ]; then
        echo "Skipping $dir (already populated)"
        return
    fi

    if [ -n "$branch" ]; then
        git clone --branch "$branch" --depth 1 "$url" "$dir"
    else
        git clone "$url" "$dir"
    fi
}

clone_if_empty tinyobjloader https://github.com/tinyobjloader/tinyobjloader.git v1.0.6
clone_if_empty stb           https://github.com/nothings/stb.git
clone_if_empty imgui         https://github.com/ocornut/imgui.git               v1.92.8
clone_if_empty entt          https://github.com/skypjack/entt.git               v3.16.0
clone_if_empty KTX-Software  https://github.com/KhronosGroup/KTX-Software.git  v4.4.2
clone_if_empty JoltPhysics   https://github.com/jrouwe/JoltPhysics.git          v5.5.0
clone_if_empty tinygltf      https://github.com/syoyo/tinygltf.git              v3.0.0
clone_if_empty glm           https://github.com/g-truc/glm.git                  1.0.3

echo "All dependencies ready."
