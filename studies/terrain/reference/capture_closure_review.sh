#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./build/dev-terrain-studies/studies/terrain/reference/terrain_reference}"
OUT_DIR="${2:-outputs/terrain_ref/closure}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
GRID_SIZE="${GRID_SIZE:-513}"
SECONDS=0

recipes=(
    terrain-engine-ref
    shadertoy-mountain
    shadertoy-erosion-filter
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
seeds=(0 9012 12345)
canonical_seed=9012

mkdir -p "${OUT_DIR}/shape" "${OUT_DIR}/presentation" "${OUT_DIR}/surface"

capture() {
    local output="$1"
    shift
    "${BIN}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --grid-size "${GRID_SIZE}" \
        --no-terrain-water-surface \
        "$@" \
        --output "${output}"
}

for recipe in "${recipes[@]}"; do
    shape_dir="${OUT_DIR}/shape/${recipe}"
    presentation_dir="${OUT_DIR}/presentation/${recipe}"
    surface_dir="${OUT_DIR}/surface/${recipe}"
    mkdir -p "${shape_dir}" "${presentation_dir}" "${surface_dir}"

    presentation_camera=oblique
    if [[ "${recipe}" == "shadertoy-coast-island" ]]; then
        presentation_camera=coastal-oblique
    fi

    surface_args=(--terrain-preview-surface pre-process)
    if [[ "${recipe}" == "shadertoy-erosion-filter" ]]; then
        surface_args=(--terrain-preview-surface post-erosion)
    fi

    for seed in "${seeds[@]}"; do
        capture "${shape_dir}/seed-${seed}-top-height.png" \
            --terrain-recipe "${recipe}" \
            --terrain-seed "${seed}" \
            --terrain-camera-preset top \
            --terrain-preview-color height \
            "${surface_args[@]}"

        capture "${presentation_dir}/seed-${seed}-${presentation_camera}-material.png" \
            --terrain-recipe "${recipe}" \
            --terrain-seed "${seed}" \
            --terrain-camera-preset "${presentation_camera}" \
            --terrain-preview-color material \
            "${surface_args[@]}"
    done

    capture "${surface_dir}/seed-${canonical_seed}-surface-low-material.png" \
        --terrain-recipe "${recipe}" \
        --terrain-seed "${canonical_seed}" \
        --terrain-camera-preset surface-low \
        --terrain-preview-color material \
        "${surface_args[@]}"
done

shape_inputs=()
presentation_inputs=()
surface_inputs=()
for recipe in "${recipes[@]}"; do
    presentation_camera=oblique
    if [[ "${recipe}" == "shadertoy-coast-island" ]]; then
        presentation_camera=coastal-oblique
    fi
    for seed in "${seeds[@]}"; do
        shape_inputs+=(
            -label "${recipe} / ${seed}"
            "${OUT_DIR}/shape/${recipe}/seed-${seed}-top-height.png"
        )
        presentation_inputs+=(
            -label "${recipe} / ${seed}"
            "${OUT_DIR}/presentation/${recipe}/seed-${seed}-${presentation_camera}-material.png"
        )
    done
    surface_inputs+=(
        -label "${recipe} / ${canonical_seed}"
        "${OUT_DIR}/surface/${recipe}/seed-${canonical_seed}-surface-low-material.png"
    )
done

magick montage "${shape_inputs[@]}" -geometry 360x203+8+24 -tile 3x \
    "${OUT_DIR}/shape-seed-contact-sheet.png"
magick montage "${presentation_inputs[@]}" -geometry 360x203+8+24 -tile 3x \
    "${OUT_DIR}/presentation-seed-contact-sheet.png"
magick montage "${surface_inputs[@]}" -geometry 420x236+8+24 -tile 3x \
    "${OUT_DIR}/surface-contact-sheet.png"

printf 'status=frozen\nwidth=%s\nheight=%s\ngrid_size=%s\nseeds=%s\ncanonical_seed=%s\nrecipes=%s\nwater_surface=disabled\nelapsed_seconds=%s\n' \
    "${WIDTH}" "${HEIGHT}" "${GRID_SIZE}" "${seeds[*]}" "${canonical_seed}" \
    "${recipes[*]}" "${SECONDS}" > "${OUT_DIR}/capture-summary.txt"
