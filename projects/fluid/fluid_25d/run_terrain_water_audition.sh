#!/usr/bin/env bash
set -euo pipefail

# Reproducible Terrain-Water Audition V1 runner. It deliberately applies the
# same protocol parameters, frame counts, camera, and views to every immutable
# crop. It is discovery evidence only: no routing diagnostic, source mask, or
# terrain transform participates in the simulation.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/fluid/fluid_25d/fluid_25d}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/fluid/terrain-water-audition-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-512}"
HEIGHT="${HEIGHT:-256}"
EARLY_FRAMES="${EARLY_FRAMES:-360}"
LATE_FRAMES="${LATE_FRAMES:-720}"
RAIN_MM_PER_HOUR="${RAIN_MM_PER_HOUR:-1200}"
RAIN_DURATION_SECONDS="${RAIN_DURATION_SECONDS:-6}"
SHEET_DEPTH_M="${SHEET_DEPTH_M:-0.05}"

[[ -x "${APP}" ]] || {
    printf 'fluid 2.5D audition app is missing or not executable: %s\n' "${APP}" >&2
    exit 1
}
[[ "${EARLY_FRAMES}" =~ ^[1-9][0-9]*$ && "${LATE_FRAMES}" =~ ^[1-9][0-9]*$ ]] || {
    printf 'EARLY_FRAMES and LATE_FRAMES must be positive integers\n' >&2
    exit 1
}
(( EARLY_FRAMES < LATE_FRAMES )) || {
    printf 'EARLY_FRAMES must be less than LATE_FRAMES\n' >&2
    exit 1
}

mkdir -p "${OUT_DIR}/captures" "${OUT_DIR}/profiles" "${OUT_DIR}/logs"

declare -a CASE_NAMES=(
    mountain-valley-floodplain
    rolling-hills-catchment
    rolling-lowland-branching
    canyon-one-flash
    canyon-four-basin-hypothesis
)
declare -a CASE_PATHS=(
    cache/terrain/sources/v1/presets/mountain-valley-1
    cache/terrain/sources/v1/presets/rolling-hills-1
    cache/terrain/sources/v1/presets/rolling-lowland-1
    cache/terrain/sources/v1/desert-canyon-study/canyon-candidate-1
    cache/terrain/sources/v1/desert-canyon-study/canyon-candidate-4
)
declare -a CASE_CROP_X=(1536 128 1216 1344 640)
declare -a CASE_CROP_Z=(1664 1728 1664 320 128)

protocol_args() {
    local protocol="$1"
    case "${protocol}" in
        rain-pulse)
            printf '%s\n' \
                --fluid25d-terrain-water-protocol rain-pulse \
                --fluid25d-rainfall-rate-mm-per-hour "${RAIN_MM_PER_HOUR}" \
                --fluid25d-source-active-duration-seconds "${RAIN_DURATION_SECONDS}"
            ;;
        sheet-release)
            printf '%s\n' \
                --fluid25d-terrain-water-protocol sheet-release \
                --fluid25d-sheet-depth-m "${SHEET_DEPTH_M}"
            ;;
        *)
            printf 'unsupported protocol: %s\n' "${protocol}" >&2
            return 1
            ;;
    esac
}

run_capture() {
    local case_name="$1"
    local terrain_path="$2"
    local crop_x="$3"
    local crop_z="$4"
    local protocol="$5"
    local frames="$6"
    local view="$7"
    local output="${OUT_DIR}/captures/${case_name}-${protocol}-f${frames}-${view}.png"
    local -a forcing=()
    mapfile -t forcing < <(protocol_args "${protocol}")

    local -a presentation=(--fluid25d-view diagnostics --debug-view "${view}")
    if [[ "${view}" == "catchment" ]]; then
        presentation=(--fluid25d-view catchment)
    fi

    "${APP}" \
        --headless --frames "${frames}" --width "${WIDTH}" --height "${HEIGHT}" \
        --fluid25d-scenario terrain-case --terrain-heightfield "${ROOT_DIR}/${terrain_path}" \
        --grid-width 256 --grid-height 128 \
        --fluid25d-terrain-crop-x "${crop_x}" --fluid25d-terrain-crop-z "${crop_z}" \
        "${forcing[@]}" "${presentation[@]}" --output "${output}" \
        >"${OUT_DIR}/logs/${case_name}-${protocol}-f${frames}-${view}.log" 2>&1
}

run_profile() {
    local case_name="$1"
    local terrain_path="$2"
    local crop_x="$3"
    local crop_z="$4"
    local protocol="$5"
    local prefix="${OUT_DIR}/profiles/${case_name}-${protocol}"
    local output="${OUT_DIR}/captures/${case_name}-${protocol}-f${LATE_FRAMES}-profile-catchment.png"
    local -a forcing=()
    mapfile -t forcing < <(protocol_args "${protocol}")

    "${APP}" \
        --headless --frames "${LATE_FRAMES}" --width "${WIDTH}" --height "${HEIGHT}" \
        --fluid25d-scenario terrain-case --terrain-heightfield "${ROOT_DIR}/${terrain_path}" \
        --grid-width 256 --grid-height 128 \
        --fluid25d-terrain-crop-x "${crop_x}" --fluid25d-terrain-crop-z "${crop_z}" \
        "${forcing[@]}" --fluid25d-view catchment \
        --profile-output "${prefix}" --profile-diagnostics \
        --profile-diagnostic-interval "$((LATE_FRAMES - 1))" --output "${output}" \
        >"${OUT_DIR}/logs/${case_name}-${protocol}-profile.log" 2>&1
}

{
    printf 'schema=fluid_25d_terrain_water_audition_v1\n'
    printf 'app=%s\n' "${APP}"
    printf 'app_sha256=%s\n' "$(sha256sum "${APP}" | awk '{print $1}')"
    printf 'cubey_head=%s\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf 'cubey_worktree=%s\n' "$(git -C "${ROOT_DIR}" status --porcelain | wc -l | tr -d ' ') changed_paths"
    printf 'host=%s\n' "$(hostname)"
    printf 'gpu=%s\n' "$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'grid=256x128 native-30m\n'
    printf 'early_frames=%s\n' "${EARLY_FRAMES}"
    printf 'late_frames=%s\n' "${LATE_FRAMES}"
    printf 'rainfall_rate_mm_per_hour=%s\n' "${RAIN_MM_PER_HOUR}"
    printf 'rainfall_duration_seconds=%s\n' "${RAIN_DURATION_SECONDS}"
    printf 'sheet_depth_m=%s\n' "${SHEET_DEPTH_M}"
    printf 'slow_pooled_speed_threshold_m_per_s=0.02\n'
} >"${OUT_DIR}/metadata.txt"

for index in "${!CASE_NAMES[@]}"; do
    case_name="${CASE_NAMES[index]}"
    terrain_path="${CASE_PATHS[index]}"
    crop_x="${CASE_CROP_X[index]}"
    crop_z="${CASE_CROP_Z[index]}"
    for protocol in rain-pulse sheet-release; do
        run_profile "${case_name}" "${terrain_path}" "${crop_x}" "${crop_z}" "${protocol}"
        for frames in "${EARLY_FRAMES}" "${LATE_FRAMES}"; do
            for view in catchment depth flow; do
                run_capture "${case_name}" "${terrain_path}" "${crop_x}" "${crop_z}" \
                    "${protocol}" "${frames}" "${view}"
            done
        done
    done
done

sha256sum "${OUT_DIR}/captures"/*.png >"${OUT_DIR}/captures.sha256"
{
    printf '%s\n' \
        'case_protocol,wet_cell_count,wet_cell_ratio,total_water_volume_m3,maximum_depth_m,wet_mean_depth_m,active_flow_cell_count,active_flow_cell_ratio,maximum_speed_m_per_s,active_mean_speed_m_per_s,slow_pooled_wet_fraction,cumulative_source_volume_m3,cumulative_sink_volume_m3,cumulative_boundary_outflow_volume_m3,conservation_residual_m3'
    awk -F, -v frame="$((LATE_FRAMES - 1))" '
        FNR == 1 { next }
        $1 == frame && $2 == "fluid_25d.water" {
            label = FILENAME
            sub(/^.*\//, "", label)
            sub(/\.metrics\.csv$/, "", label)
            value[label SUBSEP $3] = $4
            seen[label] = 1
        }
        END {
            for (label in seen) {
                printf "%s,%.0f,%.9f,%.9f,%.9f,%.9f,%.0f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n", \
                    label, \
                    value[label SUBSEP "wet_cell_count"], \
                    value[label SUBSEP "wet_cell_ratio"], \
                    value[label SUBSEP "total_water_volume_m3"], \
                    value[label SUBSEP "maximum_depth_m"], \
                    value[label SUBSEP "wet_mean_depth_m"], \
                    value[label SUBSEP "active_flow_cell_count"], \
                    value[label SUBSEP "active_flow_cell_ratio"], \
                    value[label SUBSEP "maximum_speed_m_per_s"], \
                    value[label SUBSEP "active_mean_speed_m_per_s"], \
                    value[label SUBSEP "slow_pooled_wet_fraction"], \
                    value[label SUBSEP "cumulative_source_volume_m3"], \
                    value[label SUBSEP "cumulative_sink_volume_m3"], \
                    value[label SUBSEP "cumulative_boundary_outflow_volume_m3"], \
                    value[label SUBSEP "conservation_residual_m3"]
            }
        }
    ' "${OUT_DIR}"/profiles/*.metrics.csv | sort
} >"${OUT_DIR}/comparison.csv"
printf 'fluid 2.5D terrain-water audition wrote %s\n' "${OUT_DIR}"
