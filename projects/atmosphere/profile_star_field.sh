#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/perf-star-field-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
FRAMES="${FRAMES:-900}"
WARMUP_FRAMES="${WARMUP_FRAMES:-120}"
REPEATS="${REPEATS:-3}"

mkdir -p "${OUT_DIR}/profiles"

SUMMARY="${OUT_DIR}/summary.csv"
INDEX="${OUT_DIR}/index.md"

windowed_fps() {
    awk '/windowed_perf:/ { for (i = 1; i <= NF; ++i) if ($i == "fps") { print $(i - 1); exit } }' "$1"
}

windowed_ms() {
    awk '/windowed_perf:/ { for (i = 1; i <= NF; ++i) if ($i == "ms") { print $(i - 1); exit } }' "$1"
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

run_case() {
    local case_name="$1"
    shift
    for repeat in $(seq 1 "${REPEATS}"); do
        local profile_prefix="${OUT_DIR}/profiles/${case_name}-r${repeat}"
        local profile_summary="${profile_prefix}.summary.txt"
        local log_file="${OUT_DIR}/${case_name}-r${repeat}.log"
        "${APP}" \
            --frames "${FRAMES}" \
            --width "${WIDTH}" \
            --height "${HEIGHT}" \
            --print-frame-stats \
            --profile-output "${profile_prefix}" \
            --profile-warmup-frames "${WARMUP_FRAMES}" \
            --profile-diagnostics \
            --atmosphere-preset night \
            --pause-time \
            --debug-view stars \
            --no-clouds \
            --no-moon \
            --no-reference-geometry \
            --milky-way-intensity 0 \
            "$@" >"${log_file}" 2>&1

        local pass_text
        pass_text="$(gpu_passes "${profile_summary}")"
        pass_text="${pass_text//\"/\"\"}"
        printf '%s,%s,%s,%s,%s,%s,"%s"\n' \
            "${case_name}" "${repeat}" "$(windowed_fps "${log_file}")" \
            "$(windowed_ms "${log_file}")" \
            "$(span_avg "${profile_summary}" cpu host.draw_frame)" \
            "$(gpu_total "${profile_summary}")" "${pass_text}" >>"${SUMMARY}"
    done
}

printf 'case,repeat,windowed_fps,windowed_ms,host_draw_frame_ms,gpu_total_ms,gpu_passes\n' \
    >"${SUMMARY}"

run_case stars-off --star-intensity 0 --star-density 0 --night-sky-mode human
run_case stars-human --star-intensity 1 --star-density 0.65 --night-sky-mode human
run_case stars-camera --star-intensity 1 --star-density 0.65 --night-sky-mode camera

{
    printf '# Star Field Performance Review\n\n'
    printf -- '- Size: `%sx%s`\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Frames: `%s`; warmup: `%s`; repeats: `%s`\n' \
        "${FRAMES}" "${WARMUP_FRAMES}" "${REPEATS}"
    printf -- '- View: isolated stars with the same atmosphere integration and post path.\n\n'
    printf '```csv\n'
    cat "${SUMMARY}"
    printf '```\n'
} >"${INDEX}"

printf 'Wrote %s\n' "${OUT_DIR}"
