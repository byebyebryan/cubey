#!/usr/bin/env bash
set -euo pipefail

# Reproducible dry-start, two-source control. This is an authored numerical
# explanation scene, not an imported-terrain or rainfall result.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/fluid/fluid_25d/fluid_25d}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/fluid/sustained-headwaters-v1-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES=900
FPS="${FPS:-30}"
PROFILE_INTERVAL="${PROFILE_INTERVAL:-29}"
DYE_FRAMES=1800
DYE_FPS=60
DYE_START_SECONDS=900
DYE_DURATION_SECONDS=60
DYE_ORACLE_FRAMES=961

[[ -x "${APP}" ]] || { printf 'fluid 2.5D app is missing: %s\n' "${APP}" >&2; exit 1; }
for value in "${WIDTH}" "${HEIGHT}" "${FRAMES}" "${FPS}" "${PROFILE_INTERVAL}"; do
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] || { printf 'capture settings must be positive integers\n' >&2; exit 1; }
done
[[ "${PROFILE_INTERVAL}" -lt "${FRAMES}" ]] || {
    printf 'PROFILE_INTERVAL must be below FRAMES\n' >&2
    exit 1
}
mkdir -p "${OUT_DIR}/captures" "${OUT_DIR}/logs" "${OUT_DIR}/profiles"

common=(
    --headless --width "${WIDTH}" --height "${HEIGHT}"
    --grid-width 65 --grid-height 33 --fluid25d-cell-size-m 4
    --fluid25d-scenario sustained-headwaters-demo --fluid25d-solver finite-volume
    --fluid25d-fixed-delta-seconds 1 --fluid25d-substeps 32
    --fluid25d-view catchment
)

capture_png() {
    local label="$1" frames="$2" view="$3"
    "${APP}" "${common[@]}" --capture png --frames "${frames}" \
        --fluid25d-catchment-view "${view}" \
        --output "${OUT_DIR}/captures/${label}.png" \
        >"${OUT_DIR}/logs/${label}.log" 2>&1
}

capture_video() {
    local label="$1" view="$2"
    "${APP}" "${common[@]}" --capture video --frames "${FRAMES}" --fps "${FPS}" \
        --fluid25d-catchment-view "${view}" \
        --output "${OUT_DIR}/captures/${label}.mp4" \
        >"${OUT_DIR}/logs/${label}.log" 2>&1
}

dye_args=(
    --fluid25d-catchment-view transport-inspection
    --fluid25d-dye-pulse-start-seconds "${DYE_START_SECONDS}"
    --fluid25d-dye-pulse-duration-seconds "${DYE_DURATION_SECONDS}"
)

capture_dye_png() {
    local label="$1" frames="$2"
    "${APP}" "${common[@]}" "${dye_args[@]}" --capture png --frames "${frames}" \
        --output "${OUT_DIR}/captures/${label}.png" \
        >"${OUT_DIR}/logs/${label}.log" 2>&1
}

capture_png dry-start-composite-f001 1 composite
capture_png developing-water-f300 300 water-isolation
capture_png outlet-arrival-water-f540 540 water-isolation
capture_png mature-composite-f900 "${FRAMES}" composite
capture_png mature-water-f900 "${FRAMES}" water-isolation
capture_png mature-flow-inspection-f900 "${FRAMES}" flow-inspection
capture_video dry-to-mature-composite composite
capture_video dry-to-mature-water-isolation water-isolation
capture_dye_png transport-branches-f1200 1200
capture_dye_png transport-outlet-f1500 1500
capture_dye_png transport-cleared-f1800 "${DYE_FRAMES}"

"${APP}" "${common[@]}" "${dye_args[@]}" --capture video \
    --frames "${DYE_FRAMES}" --fps "${DYE_FPS}" \
    --output "${OUT_DIR}/captures/headwaters-dye-transport.mp4" \
    >"${OUT_DIR}/logs/headwaters-dye-transport.log" 2>&1

"${APP}" "${common[@]}" --capture png --frames "${FRAMES}" \
    --fluid25d-catchment-view composite \
    --profile-output "${OUT_DIR}/profiles/sustained-headwaters" \
    --profile-diagnostics --profile-diagnostic-interval "${PROFILE_INTERVAL}" \
    --output "${OUT_DIR}/captures/profile-final.png" \
    >"${OUT_DIR}/logs/profile.log" 2>&1

"${APP}" "${common[@]}" "${dye_args[@]}" --capture png --frames "${DYE_FRAMES}" \
    --profile-output "${OUT_DIR}/profiles/headwaters-dye" \
    --profile-diagnostics --profile-diagnostic-interval "${PROFILE_INTERVAL}" \
    --output "${OUT_DIR}/captures/dye-profile-final.png" \
    >"${OUT_DIR}/logs/dye-profile.log" 2>&1

"${APP}" "${common[@]}" "${dye_args[@]}" --capture png \
    --frames "${DYE_ORACLE_FRAMES}" --fluid25d-gpu-oracle-validation \
    --output "${OUT_DIR}/captures/dye-gpu-oracle.png" \
    >"${OUT_DIR}/logs/dye-gpu-oracle.log" 2>&1

{
    printf 'schema=fluid_25d_sustained_headwaters_v1\n'
    printf 'app_sha256=%s\n' "$(sha256sum "${APP}" | awk '{print $1}')"
    printf 'runner_sha256=%s\n' "$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
    printf 'cubey_head=%s\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf 'cubey_worktree_changed_paths=%s\n' \
        "$(git -C "${ROOT_DIR}" status --porcelain | wc -l | tr -d ' ')"
    printf 'grid=65x33 cell_size_m=4\n'
    printf 'initial_water=dry\n'
    printf 'source_a=cell(4,8) 3x3 0.25_m3_per_s continuous\n'
    printf 'source_b=cell(4,24) 3x3 0.25_m3_per_s continuous\n'
    printf 'outlet=east_faces_y15_to_17 outflow_only\n'
    printf 'all_other_boundaries=closed\n'
    printf 'solver=finite-volume fixed_delta_seconds=1 substeps=32\n'
    printf 'hydraulic_frames=%s fps=%s profile_interval=%s\n' \
        "${FRAMES}" "${FPS}" "${PROFILE_INTERVAL}"
    printf 'dye_start_seconds=%s duration_seconds=%s both_sources=1 water_source_continuous=1\n' \
        "${DYE_START_SECONDS}" "${DYE_DURATION_SECONDS}"
    printf 'dye_frames=%s dye_fps=%s dye_oracle_frames=%s\n' \
        "${DYE_FRAMES}" "${DYE_FPS}" "${DYE_ORACLE_FRAMES}"
    printf 'capture_frame_fN=state_after_N_simulated_seconds\n'
} >"${OUT_DIR}/metadata.txt"

printf 'sustained-headwaters evidence: %s\n' "${OUT_DIR}"
