#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean-cloud-environment-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-18}"
MAP_SIZE="${MAP_SIZE:-512}"
PROBE_EXTENT="${PROBE_EXTENT:-64}"
DEBUG_VIEW="${DEBUG_VIEW:-final}"

mkdir -p "${OUT_DIR}"
printf 'file\ttitle\tsource\ttime\n' >"${OUT_DIR}/manifest.tsv"

capture() {
    local name="$1"
    local title="$2"
    local source="$3"
    local time="$4"
    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --ocean-map-size "${MAP_SIZE}" \
        --ocean-camera-preset mid \
        --time-of-day-mode solar \
        --time-hours "${time}" \
        --day-of-year 172 \
        --latitude-degrees 30 \
        --ocean-cloud-reflection-source "${source}" \
        --ocean-cloud-environment-extent "${PROBE_EXTENT}" \
        --ocean-cloud-environment-update-hz 4 \
        --debug-view "${DEBUG_VIEW}" \
        --output "${OUT_DIR}/${name}.png"
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${source}" "${time}" \
        >>"${OUT_DIR}/manifest.tsv"
}

for source in current-view cached hybrid; do
    capture "noon-${source}" "Noon ${source}" "${source}" 12.0
    capture "sunset-${source}" "Sunset ${source}" "${source}" 17.8
done

if command -v magick >/dev/null 2>&1; then
    magick montage \
        "${OUT_DIR}/noon-current-view.png" "${OUT_DIR}/noon-cached.png" \
        "${OUT_DIR}/noon-hybrid.png" "${OUT_DIR}/sunset-current-view.png" \
        "${OUT_DIR}/sunset-cached.png" "${OUT_DIR}/sunset-hybrid.png" \
        -geometry 640x360+8+8 -tile 3x2 "${OUT_DIR}/contact-sheet.png"
fi

printf 'Ocean cloud environment review written to %s\n' "${OUT_DIR}"
