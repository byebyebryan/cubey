#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/cloud/cloud}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-v1-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-broken-cumulus}"

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

capture surface-up --cloud-camera-mode surface-up
capture high-oblique --cloud-camera-mode high-oblique
capture surface --cloud-camera-mode surface
capture high --cloud-camera-mode high
capture orbit-preview --cloud-camera-mode orbit
capture surface-up-bayer --cloud-camera-mode surface-up --cloud-sampling-mode bayer
capture surface-up-no-jitter --cloud-camera-mode surface-up --cloud-sampling-mode off
capture high-oblique-bayer --cloud-camera-mode high-oblique --cloud-sampling-mode bayer
capture high-oblique-no-jitter --cloud-camera-mode high-oblique --cloud-sampling-mode off
capture raw-final --cloud-camera-mode surface-up --debug-view raw-final
capture raw-final-bayer --cloud-camera-mode surface-up --debug-view raw-final \
    --cloud-sampling-mode bayer
capture raw-final-no-jitter --cloud-camera-mode surface-up --debug-view raw-final \
    --cloud-sampling-mode off
capture weather --cloud-camera-mode surface-up --debug-view weather
capture base-density --cloud-camera-mode surface-up --debug-view base-density
capture detail-density --cloud-camera-mode surface-up --debug-view detail-density
capture density --cloud-camera-mode surface-up --debug-view density
capture transmittance --cloud-camera-mode surface-up --debug-view transmittance
capture lighting --cloud-camera-mode surface-up --debug-view lighting
capture ambient-light --cloud-camera-mode surface-up --debug-view ambient-light
capture direct-light --cloud-camera-mode surface-up --debug-view direct-light
capture phase-light --cloud-camera-mode surface-up --debug-view phase-light
capture shadow --cloud-camera-mode surface-up --debug-view shadow
capture cloud-alpha --cloud-camera-mode surface-up --debug-view cloud-alpha
capture distance --cloud-camera-mode surface-up --debug-view distance
capture metadata-distance --cloud-camera-mode surface-up --debug-view metadata-distance
capture metadata-alpha --cloud-camera-mode surface-up --debug-view metadata-alpha
capture metadata-confidence --cloud-camera-mode surface-up --debug-view metadata-confidence
capture metadata-density --cloud-camera-mode surface-up --debug-view metadata-density
capture steps --cloud-camera-mode surface-up --debug-view steps
capture background --cloud-camera-mode surface-up --debug-view background

if command -v magick >/dev/null 2>&1; then
    rm -f "${OUT_DIR}/contact-sheet.png"
    magick montage "${OUT_DIR}"/*.png -geometry 320x180+8+8 -tile 2x \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'cloud captures written to %s\n' "${OUT_DIR}"
