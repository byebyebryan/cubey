#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
GENERATOR="${2:-./build/dev/projects/terrain/terrain_generate}"
OUT_DIR="${3:-outputs/terrain/landscape-evolution-v1/review}"
GRID_SIZE="${GRID_SIZE:-513}"
RENDER_GRID_SIZE="${RENDER_GRID_SIZE:-257}"
CELL_SIZE="${CELL_SIZE:-100}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
ORACLE_SUMMARY="${ORACLE_SUMMARY:-outputs/terrain/landscape-evolution-v1/oracle/summary.json}"
SECONDS=0
seeds=(0 9012 12345)

if [[ -z "${OUT_DIR}" || "${OUT_DIR}" == "/" ]]; then
    printf 'invalid landscape review output directory: %s\n' "${OUT_DIR}" >&2
    exit 2
fi
rm -rf -- "${OUT_DIR}"
mkdir -p "${OUT_DIR}/fields/candidate" "${OUT_DIR}/fields/control" "${OUT_DIR}/renders"

for seed in "${seeds[@]}"; do
    candidate_dir="${OUT_DIR}/fields/candidate/seed-${seed}"
    control_dir="${OUT_DIR}/fields/control/seed-${seed}"
    "${GENERATOR}" --grid-size "${GRID_SIZE}" --terrain-cell-size "${CELL_SIZE}" \
        --terrain-seed "${seed}" --terrain-recipe upland-landscape-evolution-v1 \
        --terrain-export-raw --terrain-output-dir "${candidate_dir}"
    "${GENERATOR}" --grid-size "${GRID_SIZE}" --terrain-cell-size "${CELL_SIZE}" \
        --terrain-seed "${seed}" --terrain-recipe upland-broad-noise-control-v1 \
        --terrain-output-dir "${control_dir}"
done

capture() {
    local name="$1"
    local camera="$2"
    local recipe="${3:-upland-landscape-evolution-v1}"
    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --grid-size "${RENDER_GRID_SIZE}" --terrain-cell-size "${CELL_SIZE}" \
        --terrain-seed 9012 --terrain-recipe "${recipe}" \
        --terrain-camera-preset "${camera}" --debug-view surface \
        --output "${OUT_DIR}/renders/${name}.png"
}

capture candidate-oblique oblique
capture candidate-surface-low surface-low
capture candidate-profile profile
capture control-oblique oblique upland-broad-noise-control-v1

macro_inputs=()
for seed in "${seeds[@]}"; do
    field_dir="${OUT_DIR}/fields/candidate/seed-${seed}"
    macro_inputs+=(
        -label "seed ${seed} / source" "${field_dir}/source_height_m.png"
        -label "seed ${seed} / evolved" "${field_dir}/height_m.png"
        -label "seed ${seed} / slope" "${field_dir}/slope.png"
        -label "seed ${seed} / process drainage" "${field_dir}/process_drainage_area_m2.png"
    )
done
magick montage "${macro_inputs[@]}" -geometry 384x384+8+24 -tile 4x3 \
    "${OUT_DIR}/landscape-evolution-macro-sheet.png"

canonical="${OUT_DIR}/fields/candidate/seed-9012"
process_inputs=(
    -label "uplift rate" "${canonical}/uplift_rate_m_per_year.png"
    -label "fluvial speed" "${canonical}/fluvial_advection_rate_m_per_year.png"
    -label "hillslope speed" "${canonical}/hillslope_advection_rate_m_per_year.png"
    -label "thermal active" "${canonical}/thermal_active_mask.png"
    -label "analytical height" "${canonical}/analytical_height_m.png"
    -label "altitude correction" "${canonical}/altitude_correction_delta_m.png"
    -label "process delta" "${canonical}/process_delta_m.png"
    -label "final height" "${canonical}/height_m.png"
)
magick montage "${process_inputs[@]}" -geometry 384x384+8+24 -tile 4x2 \
    "${OUT_DIR}/landscape-evolution-process-sheet.png"

control="${OUT_DIR}/fields/control/seed-9012"
comparison_inputs=(
    -label "control height" "${control}/height_m.png"
    -label "candidate source" "${canonical}/source_height_m.png"
    -label "candidate height" "${canonical}/height_m.png"
    -label "control slope" "${control}/slope.png"
    -label "candidate slope" "${canonical}/slope.png"
    -label "candidate drainage" "${canonical}/process_drainage_area_m2.png"
)
magick montage "${comparison_inputs[@]}" -geometry 420x420+8+24 -tile 3x2 \
    "${OUT_DIR}/landscape-evolution-comparison-sheet.png"

render_inputs=(
    -label "candidate oblique" "${OUT_DIR}/renders/candidate-oblique.png"
    -label "candidate surface low" "${OUT_DIR}/renders/candidate-surface-low.png"
    -label "candidate profile" "${OUT_DIR}/renders/candidate-profile.png"
    -label "broad control oblique" "${OUT_DIR}/renders/control-oblique.png"
)
magick montage "${render_inputs[@]}" -geometry 640x360+8+24 -tile 2x2 \
    "${OUT_DIR}/landscape-evolution-render-sheet.png"

review_args=(--review-dir "${OUT_DIR}" --seeds "${seeds[@]}")
if [[ -f "${ORACLE_SUMMARY}" ]]; then
    review_args+=(--oracle-summary "${ORACLE_SUMMARY}")
fi
python3 projects/terrain/review_landscape_evolution.py "${review_args[@]}"

printf 'status=landscape-evolution-v1\ngrid_size=%s\nrender_grid_size=%s\ncell_size_m=%s\nseeds=%s\nelapsed_seconds=%s\n' \
    "${GRID_SIZE}" "${RENDER_GRID_SIZE}" "${CELL_SIZE}" "${seeds[*]}" "${SECONDS}" \
    > "${OUT_DIR}/capture-summary.txt"
