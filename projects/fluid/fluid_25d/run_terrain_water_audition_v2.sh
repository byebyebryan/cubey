#!/usr/bin/env bash
set -euo pipefail

# Reproducible Terrain-Water Audition V2 runner. V1 remains the historical
# virtual-pipes negative baseline; this runner is finite-volume-only and uses
# a longer, uniform physical-time protocol on the same immutable crops.
# Neither case selection nor any forcing parameter changes a terrain sample,
# paints a routing mask, or assigns a per-crop boundary policy.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/fluid/fluid_25d/fluid_25d}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/fluid/terrain-water-audition-v2-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-512}"
HEIGHT="${HEIGHT:-256}"
EARLY_FRAMES="${EARLY_FRAMES:-300}"
LATE_FRAMES="${LATE_FRAMES:-600}"
FIXED_DELTA_SECONDS="${FIXED_DELTA_SECONDS:-2}"
SUBSTEPS="${SUBSTEPS:-8}"
RAIN_MM_PER_HOUR="${RAIN_MM_PER_HOUR:-12}"
RAIN_DURATION_SECONDS="${RAIN_DURATION_SECONDS:-600}"
SHEET_DEPTH_M="${SHEET_DEPTH_M:-0.05}"
# Optional comma-separated subset for bounded pilots. It only chooses from the
# frozen candidate list; unset executes the full matrix with identical settings.
CASE_FILTER="${CASE_FILTER:-}"

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
[[ "${SUBSTEPS}" =~ ^[1-9][0-9]*$ && "${SUBSTEPS}" -le 64 ]] || {
    printf 'SUBSTEPS must be an integer in 1..64\n' >&2
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

validate_case_filter() {
    [[ -z "${CASE_FILTER}" ]] && return
    local -a requested_cases=()
    local requested_case
    local known_case
    IFS=',' read -r -a requested_cases <<<"${CASE_FILTER}"
    for requested_case in "${requested_cases[@]}"; do
        [[ -n "${requested_case}" ]] || {
            printf 'CASE_FILTER must be a comma-separated list of known case names\n' >&2
            return 1
        }
        for known_case in "${CASE_NAMES[@]}"; do
            [[ "${requested_case}" == "${known_case}" ]] && break
        done
        [[ "${requested_case}" == "${known_case}" ]] || {
            printf 'CASE_FILTER contains an unknown frozen terrain case: %s\n' "${requested_case}" >&2
            return 1
        }
    done
}

case_is_selected() {
    local case_name="$1"
    [[ -z "${CASE_FILTER}" || ",${CASE_FILTER}," == *",${case_name},"* ]]
}

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
    local output="${OUT_DIR}/captures/${case_name}-${protocol}-fv-f${frames}-${view}.png"
    local -a forcing=()
    mapfile -t forcing < <(protocol_args "${protocol}")

    local -a presentation=(--fluid25d-view diagnostics --debug-view "${view}")
    if [[ "${view}" == "catchment" ]]; then
        presentation=(--fluid25d-view catchment)
    fi

    "${APP}" \
        --headless --frames "${frames}" --width "${WIDTH}" --height "${HEIGHT}" \
        --fluid25d-solver finite-volume \
        --fluid25d-fixed-delta-seconds "${FIXED_DELTA_SECONDS}" \
        --fluid25d-substeps "${SUBSTEPS}" \
        --fluid25d-scenario terrain-case --terrain-heightfield "${ROOT_DIR}/${terrain_path}" \
        --grid-width 256 --grid-height 128 \
        --fluid25d-terrain-crop-x "${crop_x}" --fluid25d-terrain-crop-z "${crop_z}" \
        "${forcing[@]}" "${presentation[@]}" --output "${output}" \
        >"${OUT_DIR}/logs/${case_name}-${protocol}-fv-f${frames}-${view}.log" 2>&1
}

run_profile() {
    local case_name="$1"
    local terrain_path="$2"
    local crop_x="$3"
    local crop_z="$4"
    local protocol="$5"
    local prefix="${OUT_DIR}/profiles/${case_name}-${protocol}-fv"
    local output="${OUT_DIR}/captures/${case_name}-${protocol}-fv-f${LATE_FRAMES}-profile-catchment.png"
    local -a forcing=()
    mapfile -t forcing < <(protocol_args "${protocol}")

    "${APP}" \
        --headless --frames "${LATE_FRAMES}" --width "${WIDTH}" --height "${HEIGHT}" \
        --fluid25d-solver finite-volume \
        --fluid25d-fixed-delta-seconds "${FIXED_DELTA_SECONDS}" \
        --fluid25d-substeps "${SUBSTEPS}" \
        --fluid25d-scenario terrain-case --terrain-heightfield "${ROOT_DIR}/${terrain_path}" \
        --grid-width 256 --grid-height 128 \
        --fluid25d-terrain-crop-x "${crop_x}" --fluid25d-terrain-crop-z "${crop_z}" \
        "${forcing[@]}" --fluid25d-view catchment \
        --profile-output "${prefix}" --profile-diagnostics \
        --profile-diagnostic-interval "$((LATE_FRAMES - 1))" --output "${output}" \
        >"${OUT_DIR}/logs/${case_name}-${protocol}-fv-profile.log" 2>&1
}

validate_case_filter

{
    printf 'schema=fluid_25d_terrain_water_audition_v2\n'
    printf 'solver=finite-volume\n'
    printf 'runner_sha256=%s\n' "$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
    printf 'app=%s\n' "${APP}"
    printf 'app_sha256=%s\n' "$(sha256sum "${APP}" | awk '{print $1}')"
    printf 'cubey_head=%s\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf 'cubey_worktree=%s\n' "$(git -C "${ROOT_DIR}" status --porcelain | wc -l | tr -d ' ') changed_paths"
    printf 'host=%s\n' "$(hostname)"
    printf 'gpu=%s\n' "$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -n 1 || printf unavailable)"
    printf 'grid=256x128 native-30m\n'
    printf 'fixed_delta_seconds=%s\n' "${FIXED_DELTA_SECONDS}"
    printf 'substeps=%s\n' "${SUBSTEPS}"
    printf 'substep_seconds=%s\n' "$(awk -v delta="${FIXED_DELTA_SECONDS}" -v steps="${SUBSTEPS}" 'BEGIN { printf "%.9f", delta / steps }')"
    printf 'early_frames=%s\n' "${EARLY_FRAMES}"
    printf 'late_frames=%s\n' "${LATE_FRAMES}"
    printf 'early_elapsed_seconds=%s\n' "$(awk -v frames="${EARLY_FRAMES}" -v delta="${FIXED_DELTA_SECONDS}" 'BEGIN { printf "%.9f", frames * delta }')"
    printf 'late_elapsed_seconds=%s\n' "$(awk -v frames="${LATE_FRAMES}" -v delta="${FIXED_DELTA_SECONDS}" 'BEGIN { printf "%.9f", frames * delta }')"
    printf 'rainfall_rate_mm_per_hour=%s\n' "${RAIN_MM_PER_HOUR}"
    printf 'rainfall_duration_seconds=%s\n' "${RAIN_DURATION_SECONDS}"
    printf 'rainfall_total_depth_mm=%s\n' "$(awk -v rate="${RAIN_MM_PER_HOUR}" -v seconds="${RAIN_DURATION_SECONDS}" 'BEGIN { printf "%.9f", rate * seconds / 3600.0 }')"
    printf 'sheet_depth_m=%s\n' "${SHEET_DEPTH_M}"
    printf 'all_outward_boundary_faces=open-outflow-dry-same-bed\n'
    printf 'case_filter=%s\n' "${CASE_FILTER:-all}"
    printf 'slow_pooled_speed_threshold_m_per_s=0.02\n'
} >"${OUT_DIR}/metadata.txt"

selected_cases=0
for index in "${!CASE_NAMES[@]}"; do
    case_name="${CASE_NAMES[index]}"
    case_is_selected "${case_name}" || continue
    selected_cases=$((selected_cases + 1))
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

(( selected_cases > 0 )) || {
    printf 'CASE_FILTER selected no known frozen terrain case: %s\n' "${CASE_FILTER}" >&2
    exit 1
}

sha256sum "${OUT_DIR}/captures"/*.png >"${OUT_DIR}/captures.sha256"
{
    printf '%s\n' \
        'case_protocol,finite_volume_status_flags,wet_cell_count,wet_cell_ratio,total_water_volume_m3,maximum_depth_m,wet_mean_depth_m,active_flow_cell_count,active_flow_cell_ratio,maximum_speed_m_per_s,active_mean_speed_m_per_s,slow_pooled_wet_fraction,cumulative_source_volume_m3,cumulative_sink_volume_m3,cumulative_boundary_outflow_volume_m3,conservation_residual_m3'
    awk -F, -v frame="$((LATE_FRAMES - 1))" '
        FNR == 1 { next }
        $1 == frame && (($2 == "fluid_25d.water") ||
                        ($2 == "fluid_25d.solver" && $3 == "finite_volume_status_flags")) {
            label = FILENAME
            sub(/^.*\//, "", label)
            sub(/\.metrics\.csv$/, "", label)
            value[label SUBSEP $3] = $4
            seen[label] = 1
        }
        END {
            for (label in seen) {
                status_key = label SUBSEP "finite_volume_status_flags"
                if (!(status_key in value)) {
                    printf "missing finite-volume status metric for %s\n", label > "/dev/stderr"
                    failure = 1
                    continue
                }
                printf "%s,%.0f,%.0f,%.9f,%.9f,%.9f,%.9f,%.0f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n", \
                    label, \
                    value[label SUBSEP "finite_volume_status_flags"], \
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
            if (failure) {
                exit 1
            }
        }
    ' "${OUT_DIR}"/profiles/*.metrics.csv | sort
} >"${OUT_DIR}/comparison.csv"
printf 'fluid 2.5D Terrain-Water Audition V2 wrote %s\n' "${OUT_DIR}"
