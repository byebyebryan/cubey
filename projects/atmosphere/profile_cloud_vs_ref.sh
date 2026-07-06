#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ATMO_APP="${ATMO_APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
CLOUD_REF_APP="${CLOUD_REF_APP:-${ROOT_DIR}/build/dev/projects/cloud_ref/cloud_ref}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/perf-atmo-cloud-vs-ref-$(date +%Y%m%d-%H%M%S)}"

WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-900}"
WARMUP_FRAMES="${WARMUP_FRAMES:-120}"
REPEATS="${REPEATS:-3}"
QUALITY="${QUALITY:-full}"
VIEW_STEPS="${VIEW_STEPS:-64}"
VIEW_SAMPLES="${VIEW_SAMPLES:-1}"
WEATHER_PRESET="${WEATHER_PRESET:-broken-cumulus}"
RESOLVE_RADIUS="${RESOLVE_RADIUS:-1.5}"

mkdir -p "${OUT_DIR}/profiles"

SUMMARY="${OUT_DIR}/summary.csv"
INDEX="${OUT_DIR}/index.md"

common_cloud_args=(
    --cloud-quality "${QUALITY}"
    --cloud-view-steps "${VIEW_STEPS}"
    --cloud-view-samples "${VIEW_SAMPLES}"
    --cloud-weather-preset "${WEATHER_PRESET}"
    --cloud-resolve-mode terrain-post
    --cloud-resolve-radius-px "${RESOLVE_RADIUS}"
    --cloud-density-model surface-volume
    --cloud-distance-mode local
    --no-cloud-temporal
    --no-cloud-horizon-layer
)

common_window_args=(
    --frames "${FRAMES}"
    --width "${WIDTH}"
    --height "${HEIGHT}"
    --print-frame-stats
)

windowed_fps() {
    awk '/windowed_perf:/ { for (i = 1; i <= NF; ++i) if ($i == "fps") { print $(i - 1); exit } }' "$1"
}

windowed_ms() {
    awk '/windowed_perf:/ { for (i = 1; i <= NF; ++i) if ($i == "ms") { print $(i - 1); exit } }' "$1"
}

profile_fps() {
    awk '/average_fps:/ { print $2; exit }' "$1"
}

span_avg() {
    local summary="$1"
    local kind="$2"
    local label="$3"
    awk -F, -v kind="${kind}" -v label="${label}" \
        '$1 == kind && $2 == label { print $4; found = 1; exit } END { if (!found) print "" }' \
        "${summary}"
}

gpu_total() {
    awk -F, '$1 == "gpu" { sum += $4 } END { printf "%.6f", sum }' "$1"
}

gpu_passes() {
    awk -F, '$1 == "gpu" { if (out != "") out = out "; "; out = out $2 "=" sprintf("%.3fms", $4) } END { print out }' "$1"
}

append_summary() {
    local case_name="$1"
    local repeat="$2"
    local log_file="$3"
    local profile_prefix="$4"
    local profile_summary="${profile_prefix}.summary.txt"
    local gpu_pass_text
    gpu_pass_text="$(gpu_passes "${profile_summary}")"
    gpu_pass_text="${gpu_pass_text//\"/\"\"}"
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,"%s"\n' \
        "${case_name}" \
        "${repeat}" \
        "$(windowed_fps "${log_file}")" \
        "$(windowed_ms "${log_file}")" \
        "$(profile_fps "${profile_summary}")" \
        "$(span_avg "${profile_summary}" cpu host.draw_frame)" \
        "$(span_avg "${profile_summary}" cpu host.record_frame)" \
        "$(span_avg "${profile_summary}" cpu host.end_frame)" \
        "$(gpu_total "${profile_summary}")" \
        "${gpu_pass_text}" >>"${SUMMARY}"
}

run_case() {
    local case_name="$1"
    local app="$2"
    shift 2
    local -a args=("$@")
    for repeat in $(seq 1 "${REPEATS}"); do
        local profile_prefix="${OUT_DIR}/profiles/${case_name}-r${repeat}"
        local log_file="${OUT_DIR}/${case_name}-r${repeat}.log"
        "${app}" \
            "${common_window_args[@]}" \
            --profile-output "${profile_prefix}" \
            --profile-warmup-frames "${WARMUP_FRAMES}" \
            --profile-diagnostics \
            "${args[@]}" \
            >"${log_file}" 2>&1
        append_summary "${case_name}" "${repeat}" "${log_file}" "${profile_prefix}"
    done
}

printf 'case,repeat,windowed_fps,windowed_ms,profile_fps,host_draw_frame_ms,host_record_frame_ms,host_end_frame_ms,gpu_total_ms,gpu_passes\n' >"${SUMMARY}"

run_case "cloud-ref" "${CLOUD_REF_APP}" "${common_cloud_args[@]}"
run_case "atmo-clouds" "${ATMO_APP}" --clouds --no-moon "${common_cloud_args[@]}"
run_case "atmo-no-clouds" "${ATMO_APP}" --no-clouds --no-moon

{
    printf '# Atmosphere Cloud Performance Comparison\n\n'
    printf -- '- Output: `%s`\n' "${OUT_DIR}"
    printf -- '- Window request: `%sx%s`; logs include actual swapchain size.\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Frames: `%s`; warmup frames excluded from profile CSVs: `%s`; repeats: `%s`\n' \
        "${FRAMES}" "${WARMUP_FRAMES}" "${REPEATS}"
    printf -- '- Cloud settings: quality `%s`, view steps `%s`, view samples `%s`, weather `%s`.\n\n' \
        "${QUALITY}" "${VIEW_STEPS}" "${VIEW_SAMPLES}" "${WEATHER_PRESET}"
    printf '## Summary\n\n'
    printf '```csv\n'
    cat "${SUMMARY}"
    printf '```\n\n'
    printf 'Raw logs and profile files are next to this index. Use `*.summary.txt` for span summaries and `*.trace.json` for timeline inspection.\n'
} >"${INDEX}"

printf 'wrote %s\n' "${INDEX}"
