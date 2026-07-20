#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./build/dev-terrain-studies/studies/terrain/reference/terrain_reference}"
OUT_DIR="${2:-outputs/terrain_ref/erosion-generalization}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
GRID_SIZE="${GRID_SIZE:-513}"
SEED="${SEED:-9012}"
SECONDS=0

recipes=(
    shadertoy-alpine
    shadertoy-badlands
    shadertoy-coast-island
    shadertoy-plains
    shadertoy-dunes
)
labels=(
    alpine
    badlands
    coast-island
    plains
    dunes
)
cameras=(
    oblique
    oblique
    coastal-oblique
    oblique
    oblique
)

mkdir -p "${OUT_DIR}"

capture() {
    local output="$1"
    shift
    "${BIN}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --grid-size "${GRID_SIZE}" \
        --terrain-seed "${SEED}" \
        --no-terrain-water-surface \
        "$@" \
        --output "${output}"
}

for index in "${!recipes[@]}"; do
    recipe="${recipes[index]}"
    label="${labels[index]}"
    camera="${cameras[index]}"
    recipe_dir="${OUT_DIR}/${label}"
    mkdir -p "${recipe_dir}"

    capture "${recipe_dir}/base-material.png" \
        --terrain-recipe "${recipe}" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface pre-process \
        --terrain-preview-color material
    capture "${recipe_dir}/filtered-material.png" \
        --terrain-recipe "${recipe}" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color material
    capture "${recipe_dir}/erosion.png" \
        --terrain-recipe "${recipe}" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color erosion
    capture "${recipe_dir}/base-height.png" \
        --terrain-recipe "${recipe}" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface pre-process \
        --terrain-preview-color height
    capture "${recipe_dir}/filtered-height.png" \
        --terrain-recipe "${recipe}" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color height
done

comparison_inputs=()
for index in "${!recipes[@]}"; do
    label="${labels[index]}"
    recipe_dir="${OUT_DIR}/${label}"
    comparison_inputs+=("-label" "${label} base" "${recipe_dir}/base-material.png")
    comparison_inputs+=("-label" "${label} filtered" "${recipe_dir}/filtered-material.png")
    comparison_inputs+=("-label" "${label} erosion" "${recipe_dir}/erosion.png")
done
magick montage "${comparison_inputs[@]}" -geometry 420x236+8+24 -tile 3x \
    "${OUT_DIR}/cross-biome-contact-sheet.png"

shape_inputs=()
for index in "${!recipes[@]}"; do
    label="${labels[index]}"
    recipe_dir="${OUT_DIR}/${label}"
    shape_inputs+=("-label" "${label} base height" "${recipe_dir}/base-height.png")
    shape_inputs+=("-label" "${label} filtered height" "${recipe_dir}/filtered-height.png")
    shape_inputs+=("-label" "${label} erosion" "${recipe_dir}/erosion.png")
done
magick montage "${shape_inputs[@]}" -geometry 420x236+8+24 -tile 3x \
    "${OUT_DIR}/cross-biome-height-contact-sheet.png"

printf 'width=%s\nheight=%s\ngrid_size=%s\nseed=%s\ndiagnostic_extent_m=%s\nrecipes=%s\nelapsed_seconds=%s\n' \
    "${WIDTH}" "${HEIGHT}" "${GRID_SIZE}" "${SEED}" "360" "${recipes[*]}" "${SECONDS}" \
    > "${OUT_DIR}/capture-summary.txt"
