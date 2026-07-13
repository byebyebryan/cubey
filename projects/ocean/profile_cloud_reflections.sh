#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/perf-ocean-cloud-reflections-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-150}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"
MAP_SIZE="${MAP_SIZE:-128}"
REPEATS="${REPEATS:-1}"
PLANAR_SCALE="${PLANAR_SCALE:-0.5}"
PLANAR_STEPS="${PLANAR_STEPS:-32}"
PLANAR_GUARD="${PLANAR_GUARD:-0.15}"
sources=(cached planar)
mkdir -p "${OUT_DIR}/profiles"
summary="${OUT_DIR}/summary.csv"
printf 'source,repeat,average_fps,cloud_march_ms,cloud_environment_ms,cloud_planar_ms,ocean_scene_ms,gpu_total_ms\n' >"${summary}"

span_avg() {
    local file="$1"
    local label="$2"
    awk -F, -v label="${label}" \
        '$1 == "gpu" && $2 == label { printf "%.6f", $4; found = 1; exit } END { if (!found) printf "0.000000" }' \
        "${file}"
}

gpu_total() {
    awk -F, '$1 == "gpu" { sum += $4 } END { printf "%.6f", sum }' "$1"
}

for source in "${sources[@]}"; do
    for repeat in $(seq 1 "${REPEATS}"); do
        prefix="${OUT_DIR}/profiles/${source}-r${repeat}"
        "${APP}" \
            --headless \
            --capture video \
            --frames "${FRAMES}" \
            --fps "${FPS}" \
            --width "${WIDTH}" \
            --height "${HEIGHT}" \
            --ocean-map-size "${MAP_SIZE}" \
            --ocean-camera-preset mid \
            --time-of-day-mode solar \
            --time-hours 12 \
            --day-of-year 172 \
            --latitude-degrees 30 \
            --ocean-cloud-reflection-source "${source}" \
            --ocean-cloud-environment-extent 64 \
            --ocean-cloud-environment-update-hz 4 \
            --ocean-cloud-planar-resolution-scale "${PLANAR_SCALE}" \
            --ocean-cloud-planar-view-steps "${PLANAR_STEPS}" \
            --ocean-cloud-planar-guard-band "${PLANAR_GUARD}" \
            --profile-output "${prefix}" \
            --profile-warmup-frames "${WARMUP_FRAMES}" \
            --output "${OUT_DIR}/${source}-r${repeat}.mp4"
        profile_summary="${prefix}.summary.txt"
        average_fps="$(awk '/average_fps:/ { print $2; exit }' "${profile_summary}")"
        printf '%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "${source}" "${repeat}" "${average_fps}" \
            "$(span_avg "${profile_summary}" 'cloud march')" \
            "$(span_avg "${profile_summary}" 'ocean.cloud_environment')" \
            "$(span_avg "${profile_summary}" 'ocean.cloud_planar_reflection')" \
            "$(span_avg "${profile_summary}" 'ocean scene')" \
            "$(gpu_total "${profile_summary}")" >>"${summary}"
    done
done

{
    printf '# Ocean Cloud Reflection Performance\n\n'
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Frames: %s; warmup: %s; repeats: %s\n' "${FRAMES}" "${WARMUP_FRAMES}" "${REPEATS}"
    printf -- '- Ocean map: %s\n' "${MAP_SIZE}"
    printf -- '- Planar product: scale %s, %s steps, guard %s\n\n' \
        "${PLANAR_SCALE}" "${PLANAR_STEPS}" "${PLANAR_GUARD}"
    printf -- '- Sources: %s\n\n' "${sources[*]}"
    printf '```csv\n'
    cat "${summary}"
    printf '```\n'
} >"${OUT_DIR}/index.md"

printf 'Ocean cloud reflection profile written to %s\n' "${OUT_DIR}"
