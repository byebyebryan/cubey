#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev-terrain-studies/studies/terrain/hydrology/terrain_hydrology}"
GENERATOR="${2:-./build/dev-terrain-studies/studies/terrain/hydrology/terrain_hydrology_generate}"
OUT_DIR="${3:-outputs/terrain_hydrology_lab/v1-upland-catchment}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
GRID_SIZE="${GRID_SIZE:-257}"
SECONDS=0
seeds=(0 9012 12345)

mkdir -p "${OUT_DIR}/renders" "${OUT_DIR}/fields"

capture() {
    local output="$1"
    shift
    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --grid-size "${GRID_SIZE}" \
        "$@" \
        --output "${output}"
}

sheet_inputs=()
for seed in "${seeds[@]}"; do
    seed_dir="${OUT_DIR}/renders/seed-${seed}"
    mkdir -p "${seed_dir}"
    capture "${seed_dir}/oblique-surface.png" \
        --terrain-seed "${seed}" --terrain-camera-preset oblique --debug-view surface
    capture "${seed_dir}/top-height.png" \
        --terrain-seed "${seed}" --terrain-camera-preset top --debug-view height_m
    capture "${seed_dir}/top-discharge.png" \
        --terrain-seed "${seed}" --terrain-camera-preset top --debug-view discharge_proxy
    "${GENERATOR}" --grid-size "${GRID_SIZE}" --terrain-seed "${seed}" \
        --terrain-output-dir "${OUT_DIR}/fields/seed-${seed}"

    sheet_inputs+=(
        -label "seed ${seed} / surface" "${seed_dir}/oblique-surface.png"
        -label "seed ${seed} / height" "${seed_dir}/top-height.png"
        -label "seed ${seed} / discharge" "${seed_dir}/top-discharge.png"
    )
done

capture "${OUT_DIR}/renders/seed-9012/surface-low.png" \
    --terrain-seed 9012 --terrain-camera-preset surface-low --debug-view surface
capture "${OUT_DIR}/renders/seed-9012/top-flow-direction.png" \
    --terrain-seed 9012 --terrain-camera-preset top --debug-view flow-direction

magick montage "${sheet_inputs[@]}" -geometry 420x236+8+24 -tile 3x \
    "${OUT_DIR}/terrain-v1-contact-sheet.png"
magick montage \
    -label "seed 9012 / near surface" "${OUT_DIR}/renders/seed-9012/surface-low.png" \
    -label "seed 9012 / flow direction" "${OUT_DIR}/renders/seed-9012/top-flow-direction.png" \
    -geometry 640x360+8+24 -tile 2x "${OUT_DIR}/terrain-v1-detail-sheet.png"

printf 'recipe=upland-catchment-v1\nstatus=corrected-baseline\nwidth=%s\nheight=%s\ngrid_size=%s\nseeds=%s\nelapsed_seconds=%s\n' \
    "${WIDTH}" "${HEIGHT}" "${GRID_SIZE}" "${seeds[*]}" "${SECONDS}" \
    > "${OUT_DIR}/capture-summary.txt"
