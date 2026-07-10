#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./build/dev/projects/terrain_ref/terrain_ref}"
OUT_DIR="${2:-outputs/terrain_ref/shadertoy-erosion-filter}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
SECONDS=0

DEFAULT_DIR="${OUT_DIR}/default"
SEED_DIR="${OUT_DIR}/seed-stress"
mkdir -p "${DEFAULT_DIR}" "${SEED_DIR}"

capture() {
    local output="$1"
    shift
    "${BIN}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-recipe shadertoy-erosion-filter \
        --no-terrain-water-surface \
        "$@" \
        --output "${output}"
}

for camera in oblique surface-low; do
    capture "${DEFAULT_DIR}/${camera}-base-material.png" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface pre-process \
        --terrain-preview-color material
    capture "${DEFAULT_DIR}/${camera}-filtered-material.png" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color material
    capture "${DEFAULT_DIR}/${camera}-base-height.png" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface pre-process \
        --terrain-preview-color height
    capture "${DEFAULT_DIR}/${camera}-filtered-height.png" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color height
    capture "${DEFAULT_DIR}/${camera}-erosion.png" \
        --terrain-camera-preset "${camera}" \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color erosion
done

seeds=(0 42 9012 12345)
for seed in "${seeds[@]}"; do
    capture "${SEED_DIR}/seed-${seed}-material.png" \
        --terrain-seed "${seed}" \
        --terrain-camera-preset oblique \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color material
    capture "${SEED_DIR}/seed-${seed}-erosion.png" \
        --terrain-seed "${seed}" \
        --terrain-camera-preset oblique \
        --terrain-preview-surface post-erosion \
        --terrain-preview-color erosion
done

magick montage \
    -label "oblique base material" "${DEFAULT_DIR}/oblique-base-material.png" \
    -label "oblique filtered material" "${DEFAULT_DIR}/oblique-filtered-material.png" \
    -label "oblique erosion delta" "${DEFAULT_DIR}/oblique-erosion.png" \
    -label "surface base material" "${DEFAULT_DIR}/surface-low-base-material.png" \
    -label "surface filtered material" "${DEFAULT_DIR}/surface-low-filtered-material.png" \
    -label "surface erosion delta" "${DEFAULT_DIR}/surface-low-erosion.png" \
    -geometry 420x236+8+24 -tile 3x \
    "${OUT_DIR}/comparison-contact-sheet.png"

seed_inputs=()
for seed in "${seeds[@]}"; do
    seed_inputs+=("-label" "seed ${seed} material" "${SEED_DIR}/seed-${seed}-material.png")
    seed_inputs+=("-label" "seed ${seed} erosion" "${SEED_DIR}/seed-${seed}-erosion.png")
done
magick montage "${seed_inputs[@]}" -geometry 360x203+8+24 -tile 4x \
    "${OUT_DIR}/seed-stress-contact-sheet.png"

printf 'recipe=shadertoy-erosion-filter\nwidth=%s\nheight=%s\nseeds=%s\nelapsed_seconds=%s\n' \
    "${WIDTH}" "${HEIGHT}" "${seeds[*]}" "${SECONDS}" > "${OUT_DIR}/capture-summary.txt"

