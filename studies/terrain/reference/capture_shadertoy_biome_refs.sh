#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./build/dev-terrain-studies/studies/terrain/reference/terrain_reference}"
OUT_DIR="${2:-outputs/terrain_ref/shadertoy-biomes}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"

recipes=(
    shadertoy-alpine
    shadertoy-dunes
    shadertoy-lake-basin
    shadertoy-badlands
    shadertoy-coast-island
    shadertoy-plains
    shadertoy-gorge
    shadertoy-glacial-highland
    shadertoy-crater-field
)

cameras=(
    oblique
    surface-low
)

mkdir -p "${OUT_DIR}"

for recipe in "${recipes[@]}"; do
    recipe_dir="${OUT_DIR}/${recipe}"
    mkdir -p "${recipe_dir}"
    recipe_cameras=("${cameras[@]}")
    if [[ "${recipe}" == "shadertoy-coast-island" ]]; then
        recipe_cameras+=(coastal-oblique)
    fi
    for camera in "${recipe_cameras[@]}"; do
        "${BIN}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
            --terrain-recipe "${recipe}" \
            --terrain-camera-preset "${camera}" \
            --terrain-preview-color material \
            --terrain-water-surface \
            --output "${recipe_dir}/${camera}-material-water.png"

        "${BIN}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
            --terrain-recipe "${recipe}" \
            --terrain-camera-preset "${camera}" \
            --terrain-preview-color material \
            --no-terrain-water-surface \
            --output "${recipe_dir}/${camera}-material-dry.png"

        "${BIN}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
            --terrain-recipe "${recipe}" \
            --terrain-camera-preset "${camera}" \
            --terrain-preview-color height \
            --no-terrain-water-surface \
            --output "${recipe_dir}/${camera}-height-dry.png"
    done
done
