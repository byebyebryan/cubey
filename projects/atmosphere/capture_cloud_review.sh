#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-cloud-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-broken-cumulus}"
DEEP="${DEEP:-0}"
MOTION_FRAMES="${MOTION_FRAMES:-120}"
MOTION_FPS="${MOTION_FPS:-30}"

mkdir -p "${OUT_DIR}"

capture() {
    local name="$1"
    shift
    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"
}

capture day-final --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15
capture day-no-clouds --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --no-clouds
capture sunset-final --time-of-day-mode solar --time-hours 17.8 --camera-altitude-km 0.15
capture high-final --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 28.0 \
    --cloud-distance-mode auto
capture orbit-shell-final --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 80.0 \
    --cloud-distance-mode orbit-shell --cloud-orbit-representation surface-shell

capture raw-final --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view raw-final
capture cloud-alpha --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view cloud-alpha
capture authored-weather --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view authored-weather
capture coverage-bias --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view coverage-bias
capture local-scatter --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view local-scatter
capture local-structure --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view local-structure
capture local-edge-detail --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view local-edge-detail
capture density --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view density
capture lighting --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
    --cloud-debug-view lighting
capture distance-regime --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 28.0 \
    --cloud-distance-mode auto --cloud-debug-view distance-regime
capture transition-weights --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 28.0 \
    --cloud-distance-mode auto --cloud-debug-view transition-weights
capture orbit-coverage --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 80.0 \
    --cloud-distance-mode orbit-shell --cloud-orbit-representation surface-shell \
    --cloud-debug-view orbit-coverage
capture orbit-shell-normal --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 80.0 \
    --cloud-distance-mode orbit-shell --cloud-orbit-representation surface-shell \
    --cloud-debug-view orbit-shell-normal

if [[ "${DEEP}" != "0" ]]; then
    motion_video="${OUT_DIR}/day-motion.mp4"
    "${APP}" \
        --headless \
        --capture video \
        --frames "${MOTION_FRAMES}" \
        --fps "${MOTION_FPS}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        --time-of-day-mode solar \
        --time-hours 14.0 \
        --time-speed-hours-per-second 0.5 \
        --cloud-wind-speed-mps 900 \
        --output "${motion_video}"
    capture transmittance --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
        --cloud-debug-view transmittance
    capture ambient-light --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
        --cloud-debug-view ambient-light
    capture direct-light --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
        --cloud-debug-view direct-light
    capture phase-light --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
        --cloud-debug-view phase-light
    capture metadata-confidence --time-of-day-mode solar --time-hours 14.0 \
        --camera-altitude-km 0.15 --cloud-debug-view metadata-confidence
    capture steps --time-of-day-mode solar --time-hours 14.0 --camera-altitude-km 0.15 \
        --cloud-debug-view steps
fi

if command -v magick >/dev/null 2>&1; then
    rm -f "${OUT_DIR}/contact-sheet.png"
    magick montage "${OUT_DIR}"/*.png -geometry 320x180+8+8 -tile 2x \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'atmosphere cloud captures written to %s\n' "${OUT_DIR}"
