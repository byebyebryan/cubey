#!/usr/bin/env bash
set -euo pipefail

# Controlled windowed import evidence for legacy-immediate, one-batch, and
# incremental upload comparisons. The profile-only delayed request keeps the
# same fully initialized renderer in every observation; it is not a public
# upload mode.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/release/projects/gltf_viewer/gltf_viewer}"
ASSET_ROOT="${ASSET_ROOT:-${ROOT_DIR}/build/dev-gltf-conformance/_deps/gltf_sample_assets-src}"
ASSET_RELATIVE="${ASSET_RELATIVE:-Models/Sponza/glTF/Sponza.gltf}"
MODE_LABEL="${MODE_LABEL:-incremental}"
# Sponza's asynchronous decode can finish well after the 120-frame warmup on a
# busy development machine. Keep the default long enough to observe the upload
# fence and atomic activation; callers can still lower it for smaller assets.
FRAMES="${FRAMES:-1200}"
IMPORT_DELAY_FRAMES="${IMPORT_DELAY_FRAMES:-120}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
UPLOAD_OWNER_TARGET_MS="${UPLOAD_OWNER_TARGET_MS:-2}"
UPLOAD_STEP_BYTE_CAP="${UPLOAD_STEP_BYTE_CAP:-33554432}"
# Preserve the historical/default uncapped lane. Set this explicitly (for
# example FRAME_PACE_HZ=60) only for a paced benchmark observation.
FRAME_PACE_HZ="${FRAME_PACE_HZ:-0}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/gltf/windowed-upload-jitter-${MODE_LABEL}-$(date +%Y%m%d-%H%M%S)}"
PROFILE_PREFIX="${OUT_DIR}/gltf-viewer"

fail() {
    printf 'gltf windowed upload profile: %s\n' "$*" >&2
    exit 1
}

require_positive_integer() {
    local name="$1"
    local value="$2"
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] || fail "${name} must be a positive integer"
}

require_nonnegative_decimal() {
    local name="$1"
    local value="$2"
    [[ "${value}" =~ ^(0|[1-9][0-9]*)(\.[0-9]+)?$ ]] ||
        fail "${name} must be a nonnegative decimal (got ${value})"
}

sha256_file() {
    sha256sum "$1" | awk '{print $1}'
}

summarize_series() {
    local name="$1"
    local values="$2"
    local output="$3"
    local count
    count="$(wc -l <"${values}")"
    if [[ "${count}" -eq 0 ]]; then
        printf '%s: no samples\n' "${name}" >>"${output}"
        return
    fi
    awk -v name="${name}" -v count="${count}" '
        NR == 1 { min = $1 }
        { values[NR] = $1; max = $1; sum += $1 }
        END {
            p50 = int((count - 1) * 0.50) + 1
            p95 = int((count - 1) * 0.95) + 1
            p99 = int((count - 1) * 0.99) + 1
            printf "%s: count=%d mean=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f\n", \
                name, count, sum / count, values[p50], values[p95], values[p99], max
        }
    ' "${values}" >>"${output}"
}

[[ -x "${APP}" ]] || fail "viewer is not executable: ${APP}"
[[ -f "${ASSET_ROOT}/${ASSET_RELATIVE}" ]] ||
    fail "profile asset is missing: ${ASSET_ROOT}/${ASSET_RELATIVE}"
[[ -n "${XDG_RUNTIME_DIR:-}" && -n "${WAYLAND_DISPLAY:-}" ]] ||
    fail "set XDG_RUNTIME_DIR and WAYLAND_DISPLAY for the controlled windowed lane"
[[ -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]] ||
    fail "Wayland socket is unavailable: ${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}"
require_positive_integer FRAMES "${FRAMES}"
require_positive_integer IMPORT_DELAY_FRAMES "${IMPORT_DELAY_FRAMES}"
require_nonnegative_decimal FRAME_PACE_HZ "${FRAME_PACE_HZ}"
[[ "${FRAMES}" -gt "${IMPORT_DELAY_FRAMES}" ]] ||
    fail "FRAMES must exceed IMPORT_DELAY_FRAMES"
[[ ! -e "${OUT_DIR}" ]] || fail "output path already exists: ${OUT_DIR}"
mkdir -p "${OUT_DIR}"

FRAME_PACE_ENABLED=1
FRAME_PACE_METADATA="${FRAME_PACE_HZ}"
if [[ "${FRAME_PACE_HZ}" =~ ^0(\.0+)?$ ]]; then
    FRAME_PACE_ENABLED=0
    FRAME_PACE_METADATA=0
fi

command=(
    "${APP}"
    --frames "${FRAMES}"
    --width "${WIDTH}"
    --height "${HEIGHT}"
    --input "${ASSET_ROOT}/${ASSET_RELATIVE}"
    --pbr-environment-source static
    --no-clouds
    --no-ocean-backdrop
    --profile-import-delay-frames "${IMPORT_DELAY_FRAMES}"
    --profile-upload-owner-target-ms "${UPLOAD_OWNER_TARGET_MS}"
    --profile-upload-step-byte-cap "${UPLOAD_STEP_BYTE_CAP}"
    --profile-output "${PROFILE_PREFIX}"
    --profile-warmup-frames 0
)
if (( FRAME_PACE_ENABLED )); then
    command+=(--profile-frame-pace-hz "${FRAME_PACE_HZ}")
fi

{
    printf 'mode=%s\n' "${MODE_LABEL}"
    printf 'app=%s\n' "${APP}"
    printf 'app_sha256=%s\n' "$(sha256_file "${APP}")"
    printf 'asset_root=%s\n' "${ASSET_ROOT}"
    printf 'asset_relative=%s\n' "${ASSET_RELATIVE}"
    printf 'asset_sha256=%s\n' "$(sha256_file "${ASSET_ROOT}/${ASSET_RELATIVE}")"
    printf 'wayland_display=%s\n' "${WAYLAND_DISPLAY}"
    printf 'frames=%s\n' "${FRAMES}"
    printf 'import_delay_frames=%s\n' "${IMPORT_DELAY_FRAMES}"
    printf 'width=%s\nheight=%s\n' "${WIDTH}" "${HEIGHT}"
    printf 'upload_owner_target_ms=%s\n' "${UPLOAD_OWNER_TARGET_MS}"
    printf 'upload_step_byte_cap=%s\n' "${UPLOAD_STEP_BYTE_CAP}"
    printf 'upload_copy_byte_target=2097152\n'
    printf 'frame_pace_hz=%s\n' "${FRAME_PACE_METADATA}"
} >"${OUT_DIR}/metadata.txt"
printf '%q ' "${command[@]}" >"${OUT_DIR}/command.txt"
printf '\n' >>"${OUT_DIR}/command.txt"

"${command[@]}" >"${OUT_DIR}/viewer.log" 2>&1
for suffix in frames.csv passes.csv metrics.csv trace.json summary.txt; do
    [[ -f "${PROFILE_PREFIX}.${suffix}" ]] || fail "viewer did not write ${suffix}"
done

summary="${OUT_DIR}/jitter-summary.txt"
printf 'measurement starts at application frame %s\n' "${IMPORT_DELAY_FRAMES}" >"${summary}"
for item in \
    "frame_delta_ms,${PROFILE_PREFIX}.frames.csv,2" \
    "host_update_ms,${PROFILE_PREFIX}.passes.csv,5,host.update" \
    "host_gpu_drain_ms,${PROFILE_PREFIX}.passes.csv,5,host.gpu_drain" \
    "host_draw_frame_ms,${PROFILE_PREFIX}.passes.csv,5,host.draw_frame" \
    "gltf_asset_build_poll_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.asset_build_poll" \
    "gltf_session_completion_adoption_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.session_completion_adoption" \
    "gltf_scene_activation_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.scene_activation" \
    "gltf_scene_retirement_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.scene_retirement" \
    "gltf_activation_metric_queue_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.activation_metric_queue" \
    "gltf_activation_log_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.activation_log" \
    "gltf_activation_cpu_disposal_enqueue_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.activation_cpu_disposal_enqueue" \
    "gltf_atmosphere_atlas_poll_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.atmosphere_atlas_poll" \
    "gltf_animation_update_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.animation_update" \
    "gltf_atmosphere_update_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.atmosphere_update" \
    "gltf_camera_update_ms,${PROFILE_PREFIX}.passes.csv,5,gltf.camera_update"; do
    IFS=, read -r name source _ label <<<"${item}"
    values="${OUT_DIR}/${name}.values"
    if [[ -n "${label:-}" ]]; then
        awk -F, -v first="${IMPORT_DELAY_FRAMES}" -v label="${label}" \
            'NR > 1 && $1 >= first && $3 == label { print $5 }' "${source}" | sort -n >"${values}"
    else
        awk -F, -v first="${IMPORT_DELAY_FRAMES}" \
            'NR > 1 && $1 >= first { print $2 }' "${source}" | sort -n >"${values}"
    fi
    summarize_series "${name}" "${values}" "${summary}"
done
printf '\ngltf_loading metrics:\n' >>"${summary}"
awk -F, 'NR > 1 && $2 == "gltf_loading" { print $1 "," $3 "," $4 }' \
    "${PROFILE_PREFIX}.metrics.csv" >>"${summary}"

printf 'gltf windowed upload profile: wrote %s\n' "${OUT_DIR}"
