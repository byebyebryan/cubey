#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev-terrain-studies/studies/terrain/hydrology/terrain_hydrology}"
GENERATOR="${2:-./build/dev-terrain-studies/studies/terrain/hydrology/terrain_hydrology_generate}"
OUT_DIR="${3:-outputs/terrain_hydrology_lab/source-bakeoff-v1}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
GRID_SIZE="${GRID_SIZE:-257}"
REGIONAL_GRID_SIZE="${REGIONAL_GRID_SIZE:-769}"
REGIONAL_ORIGIN_X="${REGIONAL_ORIGIN_X:-0}"
REGIONAL_ORIGIN_Z="${REGIONAL_ORIGIN_Z:-0}"
SECONDS=0
seeds=(0 9012 12345)
recipes=(upland-catchment-v1 upland-broad-noise-control-v1)
slugs=(contour-baseline broad-noise-control)
labels=("contour baseline" "broad-noise control")

if [[ -z "${OUT_DIR}" || "${OUT_DIR}" == "/" ]]; then
    printf 'invalid terrain review output directory: %s\n' "${OUT_DIR}" >&2
    exit 2
fi
rm -rf -- "${OUT_DIR}"
mkdir -p "${OUT_DIR}/renders" "${OUT_DIR}/fields" "${OUT_DIR}/regional"

capture() {
    local output="$1"
    shift
    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --grid-size "${GRID_SIZE}" \
        "$@" \
        --output "${output}"
}

sheet_inputs=()
detail_inputs=()
manifests=()
for recipe_index in "${!recipes[@]}"; do
    recipe="${recipes[recipe_index]}"
    slug="${slugs[recipe_index]}"
    label="${labels[recipe_index]}"
    for seed in "${seeds[@]}"; do
        field_dir="${OUT_DIR}/fields/${slug}/seed-${seed}"
        render_dir="${OUT_DIR}/renders/${slug}/seed-${seed}"
        mkdir -p "${field_dir}" "${render_dir}"
        "${GENERATOR}" --grid-size "${GRID_SIZE}" --terrain-seed "${seed}" \
            --terrain-recipe "${recipe}" --terrain-output-dir "${field_dir}"
        capture "${render_dir}/oblique-surface.png" \
            --terrain-seed "${seed}" --terrain-recipe "${recipe}" \
            --terrain-camera-preset oblique --debug-view surface
        sheet_inputs+=(
            -label "${label} / ${seed} / surface" "${render_dir}/oblique-surface.png"
            -label "${label} / ${seed} / source" "${field_dir}/source_height_m.png"
            -label "${label} / ${seed} / slope" "${field_dir}/slope.png"
            -label "${label} / ${seed} / fill" "${field_dir}/routing_fill_delta_m.png"
        )
        manifests+=("${field_dir}/manifest.json")
    done
    low_path="${OUT_DIR}/renders/${slug}/seed-9012/surface-low.png"
    capture "${low_path}" --terrain-seed 9012 --terrain-recipe "${recipe}" \
        --terrain-camera-preset surface-low --debug-view surface
    detail_inputs+=(-label "${label} / surface low" "${low_path}")

    regional_dir="${OUT_DIR}/regional/${slug}"
    "${GENERATOR}" --grid-size "${REGIONAL_GRID_SIZE}" --terrain-seed 9012 \
        --terrain-recipe "${recipe}" --terrain-origin-x "${REGIONAL_ORIGIN_X}" \
        --terrain-origin-z "${REGIONAL_ORIGIN_Z}" --terrain-output-dir "${regional_dir}"
done

magick montage "${sheet_inputs[@]}" -geometry 320x180+6+22 -tile 4x6 \
    "${OUT_DIR}/terrain-source-bakeoff-contact-sheet.png"
magick montage "${detail_inputs[@]}" -geometry 640x360+8+24 -tile 2x \
    "${OUT_DIR}/terrain-source-bakeoff-surface-sheet.png"

regional_inputs=()
for recipe_index in "${!recipes[@]}"; do
    slug="${slugs[recipe_index]}"
    label="${labels[recipe_index]}"
    regional_dir="${OUT_DIR}/regional/${slug}"
    regional_inputs+=(
        -label "${label} / source" "${regional_dir}/source_height_m.png"
        -label "${label} / slope" "${regional_dir}/slope.png"
        -label "${label} / fill" "${regional_dir}/routing_fill_delta_m.png"
        -label "${label} / area" "${regional_dir}/contributing_area_m2.png"
    )
done
magick montage "${regional_inputs[@]}" -geometry 480x480+8+24 -tile 4x2 \
    "${OUT_DIR}/terrain-source-bakeoff-regional-sheet.png"

jq -s '{schema: "cubey.terrain.source-bakeoff.v1", patches: .}' "${manifests[@]}" \
    > "${OUT_DIR}/comparison-summary.json"
printf 'status=source-bakeoff\nrecipes=%s\nwidth=%s\nheight=%s\ngrid_size=%s\nregional_grid_size=%s\nseeds=%s\nelapsed_seconds=%s\n' \
    "${recipes[*]}" "${WIDTH}" "${HEIGHT}" "${GRID_SIZE}" "${REGIONAL_GRID_SIZE}" \
    "${seeds[*]}" "${SECONDS}" > "${OUT_DIR}/capture-summary.txt"
