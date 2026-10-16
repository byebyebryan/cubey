#!/usr/bin/env bash
set -euo pipefail

# Reproducible review runner for the separate immutable-terrain mountain
# source/outlet demonstration. It never changes the terrain raster, solver
# defaults, or neutral terrain-case audition protocol; all source, drain, and
# prewetted-route fields belong to the explicitly selected product scenario.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/fluid/fluid_25d/fluid_25d}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/fluid/mountain-source-outlet-v1-$(date +%Y%m%d-%H%M%S)}"
TERRAIN_PATH="${ROOT_DIR}/cache/terrain/sources/v1/presets/mountain-valley-1"
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
MATURE_FRAMES="${MATURE_FRAMES:-600}"
ORACLE_FRAMES="${ORACLE_FRAMES:-120}"
FIXED_DELTA_SECONDS="${FIXED_DELTA_SECONDS:-2}"
SUBSTEPS="${SUBSTEPS:-8}"
PROFILE_INTERVAL="${PROFILE_INTERVAL:-60}"

[[ -x "${APP}" ]] || {
    printf 'fluid 2.5D app is missing or not executable: %s\n' "${APP}" >&2
    exit 1
}
[[ -f "${TERRAIN_PATH}/heightfield.json" ]] || {
    printf 'pinned mountain heightfield is missing: %s\n' "${TERRAIN_PATH}" >&2
    exit 1
}
[[ "${MATURE_FRAMES}" =~ ^[1-9][0-9]*$ && "${ORACLE_FRAMES}" =~ ^[1-9][0-9]*$ ]] || {
    printf 'MATURE_FRAMES and ORACLE_FRAMES must be positive integers\n' >&2
    exit 1
}
[[ "${FIXED_DELTA_SECONDS}" =~ ^([0-9]+([.][0-9]*)?|[.][0-9]+)$ ]] &&
    awk -v value="${FIXED_DELTA_SECONDS}" 'BEGIN { exit !(value > 0.0) }' || {
    printf 'FIXED_DELTA_SECONDS must be a positive decimal number\n' >&2
    exit 1
}
[[ "${PROFILE_INTERVAL}" =~ ^[1-9][0-9]*$ && "${PROFILE_INTERVAL}" -lt "${MATURE_FRAMES}" ]] || {
    printf 'PROFILE_INTERVAL must be a positive integer below MATURE_FRAMES\n' >&2
    exit 1
}
[[ "${SUBSTEPS}" =~ ^[1-9][0-9]*$ && "${SUBSTEPS}" -le 64 ]] || {
    printf 'SUBSTEPS must be an integer in 1..64\n' >&2
    exit 1
}

mkdir -p "${OUT_DIR}/captures" "${OUT_DIR}/profiles" "${OUT_DIR}/logs"

common_args=(
    --headless --capture png --width "${WIDTH}" --height "${HEIGHT}"
    --fluid25d-scenario mountain-source-outlet-demo
    --fluid25d-solver finite-volume
    --terrain-heightfield "${TERRAIN_PATH}"
    --fluid25d-fixed-delta-seconds "${FIXED_DELTA_SECONDS}"
    --fluid25d-substeps "${SUBSTEPS}"
    --fluid25d-view catchment
)

run_capture() {
    local label="$1"
    local frames="$2"
    local catchment_view="$3"
    "${APP}" "${common_args[@]}" --frames "${frames}" \
        --fluid25d-catchment-view "${catchment_view}" \
        --output "${OUT_DIR}/captures/${label}.png" \
        >"${OUT_DIR}/logs/${label}.log" 2>&1
}

run_profile() {
    local label="$1"
    local interval="$2"
    local prefix="${OUT_DIR}/profiles/${label}"
    "${APP}" "${common_args[@]}" --frames "${MATURE_FRAMES}" \
        --fluid25d-catchment-view composite \
        --profile-output "${prefix}" --profile-diagnostics \
        --profile-diagnostic-interval "${interval}" \
        --output "${OUT_DIR}/captures/${label}.png" \
        >"${OUT_DIR}/logs/${label}.log" 2>&1
}

MATURE_TAG="f${MATURE_FRAMES}"
run_capture mountain-reset-composite-f001 1 composite
run_capture "mountain-mature-composite-${MATURE_TAG}" "${MATURE_FRAMES}" composite
run_capture "mountain-mature-water-isolation-${MATURE_TAG}" "${MATURE_FRAMES}" water-isolation
run_capture "mountain-mature-flow-inspection-${MATURE_TAG}" "${MATURE_FRAMES}" flow-inspection

"${APP}" "${common_args[@]}" --frames "${ORACLE_FRAMES}" \
    --fluid25d-catchment-view composite --fluid25d-gpu-oracle-validation \
    --output "${OUT_DIR}/captures/mountain-gpu-oracle-f${ORACLE_FRAMES}.png" \
    >"${OUT_DIR}/logs/mountain-gpu-oracle-f${ORACLE_FRAMES}.log" 2>&1

run_profile "mountain-profile-${MATURE_TAG}-i${PROFILE_INTERVAL}" "${PROFILE_INTERVAL}"
run_profile "mountain-profile-${MATURE_TAG}-final" "$((MATURE_FRAMES - 1))"

metadata="${OUT_DIR}/metadata.txt"
{
    printf 'schema=fluid_25d_mountain_source_outlet_v1\n'
    printf 'runner_sha256=%s\n' "$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
    printf 'app=%s\n' "${APP}"
    printf 'app_sha256=%s\n' "$(sha256sum "${APP}" | awk '{print $1}')"
    printf 'cubey_head=%s\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf 'cubey_worktree=%s changed_paths\n' "$(git -C "${ROOT_DIR}" status --porcelain | wc -l | tr -d ' ')"
    printf 'terrain_path=%s\n' "${TERRAIN_PATH}"
    printf 'terrain_elevation_sha256=2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48\n'
    printf 'terrain_transformed_crop_sha256=9bfebfe229886ded533556acaf11de541caddfc4cf8104d864da1232fa8b24c6\n'
    printf 'crop=1536,1664,256x128\n'
    printf 'grid=256x128 native-30m\n'
    printf 'solver=finite-volume\n'
    printf 'fixed_delta_seconds=%s\n' "${FIXED_DELTA_SECONDS}"
    printf 'substeps=%s\n' "${SUBSTEPS}"
    printf 'substep_seconds=%s\n' "$(awk -v delta="${FIXED_DELTA_SECONDS}" -v steps="${SUBSTEPS}" 'BEGIN { printf "%.9f", delta / steps }')"
    printf 'mature_frames=%s\n' "${MATURE_FRAMES}"
    printf 'mature_elapsed_seconds=%s\n' "$(awk -v frames="${MATURE_FRAMES}" -v delta="${FIXED_DELTA_SECONDS}" 'BEGIN { printf "%.9f", frames * delta }')"
    printf 'source=cell(8,60),81-cells,total-0.75-m3-per-s\n'
    printf 'visible_outlet=cell(232,122),81-cells\n'
    printf 'explicit_drain=cells(232,127),(229,126),(231,126),total-0.75-m3-per-s\n'
    printf 'outer_boundary=closed\n'
    printf 'initial_water=reviewed-radius-4-prewetted-route-not-parcel-proof\n'
} >"${metadata}"

profile_interval_prefix="${OUT_DIR}/profiles/mountain-profile-${MATURE_TAG}-i${PROFILE_INTERVAL}"
profile_final_prefix="${OUT_DIR}/profiles/mountain-profile-${MATURE_TAG}-final"
interval_metrics="${profile_interval_prefix}.metrics.csv"
final_metrics="${profile_final_prefix}.metrics.csv"
[[ -f "${interval_metrics}" && -f "${final_metrics}" ]] || {
    printf 'expected profile metrics were not written\n' >&2
    exit 1
}

last_profile_frame=$((MATURE_FRAMES - 1 - ((MATURE_FRAMES - 1) % PROFILE_INTERVAL)))
previous_frame=$((last_profile_frame - PROFILE_INTERVAL))
awk -F, -v prior="${previous_frame}" -v last="${last_profile_frame}" \
    -v fixed_delta_seconds="${FIXED_DELTA_SECONDS}" '
    BEGIN {
        seconds = (last - prior) * fixed_delta_seconds
        if (!(seconds > 0.0)) {
            printf "late-window duration is not positive\n" > "/dev/stderr"
            exit 1
        }
    }
    $1 == prior && $2 == "fluid_25d.water" { prior_value[$3] = $4 }
    $1 == last && $2 == "fluid_25d.water" { last_value[$3] = $4 }
    END {
        source_rate = (last_value["cumulative_source_volume_m3"] - prior_value["cumulative_source_volume_m3"]) / seconds
        sink_rate = (last_value["cumulative_sink_volume_m3"] - prior_value["cumulative_sink_volume_m3"]) / seconds
        printf "late_window_frames=%d..%d\n", prior, last
        printf "late_window_seconds=%.9f\n", seconds
        printf "late_source_rate_m3_per_s=%.9f\n", source_rate
        printf "late_sink_rate_m3_per_s=%.9f\n", sink_rate
        printf "late_rate_relative_difference=%.9f\n", (source_rate - sink_rate) / 0.75
        rate_difference = source_rate - sink_rate
        if (rate_difference < 0.0) {
            rate_difference = -rate_difference
        }
        if (!(source_rate >= 0.70 && source_rate <= 0.80 && sink_rate >= 0.70 &&
              sink_rate <= 0.80 && rate_difference <= 0.015)) {
            printf "late source/sink acceptance failed\n" > "/dev/stderr"
            exit 1
        }
    }
' "${interval_metrics}" >"${OUT_DIR}/acceptance.txt"
awk -F, -v final="$((MATURE_FRAMES - 1))" '
    $1 == final && (($2 == "fluid_25d.solver" && $3 == "finite_volume_status_flags") ||
                    ($2 == "fluid_25d.water" && ($3 == "cumulative_source_volume_m3" ||
                                                    $3 == "cumulative_sink_volume_m3" ||
                                                    $3 == "cumulative_boundary_outflow_volume_m3" ||
                                                    $3 == "total_water_volume_m3" ||
                                                    $3 == "maximum_depth_m" ||
                                                    $3 == "maximum_speed_m_per_s"))) {
        printf "final_%s=%s\n", $3, $4
    }
    $1 == final && $2 == "fluid_25d.solver" && $3 == "finite_volume_status_flags" {
        status = $4
        have_status = 1
    }
    $1 == final && $2 == "fluid_25d.water" && $3 == "cumulative_boundary_outflow_volume_m3" {
        boundary = $4
        have_boundary = 1
    }
    END {
        if (!have_status || !have_boundary || status != 0.0 || boundary != 0.0) {
            printf "final finite-volume status or closed-boundary acceptance failed\n" > "/dev/stderr"
            exit 1
        }
    }
' "${final_metrics}" >>"${OUT_DIR}/acceptance.txt"

sha256sum "${OUT_DIR}/captures"/*.png >"${OUT_DIR}/captures.sha256"
printf 'fluid 2.5D mountain source/outlet review wrote %s\n' "${OUT_DIR}"
