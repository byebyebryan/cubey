#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/clouds/clouds}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/clouds-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-half}"
PRESET="${PRESET:-broken-cumulus}"
TIME_HOURS="${TIME_HOURS:-14.0}"

mkdir -p "${OUT_DIR}"

capture_at_time() {
    local name="$1"
    local time_hours="$2"
    shift 2
    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        --time-of-day-mode solar \
        --time-hours "${time_hours}" \
        --pause-time \
        "$@" \
        --output "${OUT_DIR}/${name}.png"
}

capture() {
    local name="$1"
    shift
    capture_at_time "${name}" "${TIME_HOURS}" "$@"
}

capture surface --cloud-camera-mode surface
capture surface-raw --cloud-camera-mode surface --no-cloud-temporal
capture surface-up --cloud-camera-mode surface-up
capture high --cloud-camera-mode high
capture high-oblique --cloud-camera-mode high-oblique
capture orbit --cloud-camera-mode orbit
capture orbit-terminator --cloud-camera-mode orbit-terminator
capture weather --cloud-camera-mode orbit --debug-view weather
capture density --cloud-camera-mode high-oblique --debug-view density
capture base-density --cloud-camera-mode surface --debug-view base-density
capture detail-density --cloud-camera-mode surface --debug-view detail-density
capture density-lod --cloud-camera-mode surface --debug-view density-lod
capture step-length --cloud-camera-mode surface --debug-view step-length
capture local-march --cloud-camera-mode surface --debug-view local-march
capture far-horizon --cloud-camera-mode surface --debug-view far-horizon
capture cloud-alpha --cloud-camera-mode high-oblique --debug-view cloud-alpha
capture domain --cloud-camera-mode high-oblique --debug-view domain
capture distance --cloud-camera-mode high-oblique --debug-view distance
capture surface-shadow --cloud-camera-mode surface --debug-view surface-shadow
capture cloud-depth --cloud-camera-mode surface --debug-view cloud-depth
capture cloud-confidence --cloud-camera-mode surface --debug-view cloud-confidence
capture horizon-blend --cloud-camera-mode surface --debug-view horizon-blend
capture_at_time orbit-night 0.0 --cloud-camera-mode orbit

if command -v magick >/dev/null 2>&1; then
    magick montage \
        "${OUT_DIR}/surface.png" \
        "${OUT_DIR}/surface-raw.png" \
        "${OUT_DIR}/surface-up.png" \
        "${OUT_DIR}/high.png" \
        "${OUT_DIR}/high-oblique.png" \
        "${OUT_DIR}/orbit.png" \
        "${OUT_DIR}/orbit-terminator.png" \
        "${OUT_DIR}/weather.png" \
        "${OUT_DIR}/density.png" \
        "${OUT_DIR}/base-density.png" \
        "${OUT_DIR}/detail-density.png" \
        "${OUT_DIR}/density-lod.png" \
        "${OUT_DIR}/step-length.png" \
        "${OUT_DIR}/local-march.png" \
        "${OUT_DIR}/far-horizon.png" \
        "${OUT_DIR}/cloud-depth.png" \
        "${OUT_DIR}/cloud-confidence.png" \
        "${OUT_DIR}/horizon-blend.png" \
        "${OUT_DIR}/cloud-alpha.png" \
        "${OUT_DIR}/domain.png" \
        "${OUT_DIR}/distance.png" \
        "${OUT_DIR}/surface-shadow.png" \
        "${OUT_DIR}/orbit-night.png" \
        -geometry 480x270+12+32 \
        -tile 5x5 \
        "${OUT_DIR}/contact-sheet.png"
elif command -v montage >/dev/null 2>&1; then
    montage \
        "${OUT_DIR}/surface.png" \
        "${OUT_DIR}/surface-raw.png" \
        "${OUT_DIR}/surface-up.png" \
        "${OUT_DIR}/high.png" \
        "${OUT_DIR}/high-oblique.png" \
        "${OUT_DIR}/orbit.png" \
        "${OUT_DIR}/orbit-terminator.png" \
        "${OUT_DIR}/weather.png" \
        "${OUT_DIR}/density.png" \
        "${OUT_DIR}/base-density.png" \
        "${OUT_DIR}/detail-density.png" \
        "${OUT_DIR}/density-lod.png" \
        "${OUT_DIR}/step-length.png" \
        "${OUT_DIR}/local-march.png" \
        "${OUT_DIR}/far-horizon.png" \
        "${OUT_DIR}/cloud-depth.png" \
        "${OUT_DIR}/cloud-confidence.png" \
        "${OUT_DIR}/horizon-blend.png" \
        "${OUT_DIR}/cloud-alpha.png" \
        "${OUT_DIR}/domain.png" \
        "${OUT_DIR}/distance.png" \
        "${OUT_DIR}/surface-shadow.png" \
        "${OUT_DIR}/orbit-night.png" \
        -geometry 480x270+12+32 \
        -tile 5x5 \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'cloud review captures: %s\n' "${OUT_DIR}"
