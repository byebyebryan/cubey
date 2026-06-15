#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/cloud_ref_2/cloud_ref_2}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-ref-2-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-70}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-broken-cumulus}"
CACHE_FRAMES="${CACHE_FRAMES:-64}"
CACHE_TEXTURE_SIZE="${CACHE_TEXTURE_SIZE:-768}"
CACHE_MATRIX="${CACHE_MATRIX:-0}"

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
        --cloud-cache-frames "${CACHE_FRAMES}" \
        --cloud-cache-texture-size "${CACHE_TEXTURE_SIZE}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"
}

capture surface --cloud-camera-mode surface
capture surface-up --cloud-camera-mode surface-up
capture high --cloud-camera-mode high
capture high-oblique --cloud-camera-mode high-oblique
capture orbit-preview --cloud-camera-mode orbit
capture raw-final --cloud-camera-mode surface --debug-view raw-final
capture raw-cloud-product --cloud-camera-mode surface --debug-view raw-cloud-product
capture blend-from --cloud-camera-mode surface --debug-view blend-from
capture blend-to --cloud-camera-mode surface --debug-view blend-to
capture update-region --cloud-camera-mode surface --debug-view update-region
capture oct-uv --cloud-camera-mode surface --debug-view oct-uv
capture cache-direction --cloud-camera-mode surface --debug-view cache-direction
capture cache-horizon --cloud-camera-mode surface --debug-view cache-horizon
capture cache-checker --cloud-camera-mode surface --debug-view cache-checker
capture cache-alpha --cloud-camera-mode surface --debug-view cache-alpha
capture weather --cloud-camera-mode surface --debug-view weather
capture base-density --cloud-camera-mode surface --debug-view base-density
capture detail-density --cloud-camera-mode surface --debug-view detail-density
capture density --cloud-camera-mode surface --debug-view density
capture transmittance --cloud-camera-mode surface --debug-view transmittance
capture lighting --cloud-camera-mode surface --debug-view lighting
capture ambient-light --cloud-camera-mode surface --debug-view ambient-light
capture direct-light --cloud-camera-mode surface --debug-view direct-light
capture phase-light --cloud-camera-mode surface --debug-view phase-light
capture shadow --cloud-camera-mode surface --debug-view shadow
capture cloud-alpha --cloud-camera-mode surface --debug-view cloud-alpha
capture distance --cloud-camera-mode surface --debug-view distance
capture steps --cloud-camera-mode surface --debug-view steps
capture background --cloud-camera-mode surface --debug-view background

if [[ "${CACHE_MATRIX}" == "1" ]]; then
    for camera in surface surface-up high high-oblique; do
        for frames in 4 16 64; do
            for size in 512 768 1024; do
                capture "matrix-${camera}-${frames}f-${size}-checker" \
                    --cloud-camera-mode "${camera}" \
                    --cloud-cache-frames "${frames}" \
                    --cloud-cache-texture-size "${size}" \
                    --debug-view cache-checker
            done
        done
    done
fi

if command -v magick >/dev/null 2>&1; then
    rm -f "${OUT_DIR}/contact-sheet.png"
    magick montage "${OUT_DIR}"/*.png -geometry 320x180+8+8 -tile 2x \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'cloud_ref_2 captures written to %s\n' "${OUT_DIR}"
