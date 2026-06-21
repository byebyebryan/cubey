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
DEEP="${DEEP:-0}"
MOTION_FRAMES="${MOTION_FRAMES:-120}"
MOTION_FPS="${MOTION_FPS:-30}"
CENTER_CROP_GEOMETRY="${CENTER_CROP_GEOMETRY:-}"

if [[ -z "${CENTER_CROP_GEOMETRY}" ]]; then
    crop_w=$(( WIDTH * 41 / 100 ))
    crop_h=$(( HEIGHT * 46 / 100 ))
    crop_x=$(( WIDTH * 29 / 100 ))
    crop_y=$(( HEIGHT * 38 / 100 ))
    if (( crop_w < 1 )); then
        crop_w=1
    fi
    if (( crop_h < 1 )); then
        crop_h=1
    fi
    CENTER_CROP_GEOMETRY="${crop_w}x${crop_h}+${crop_x}+${crop_y}"
fi

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
capture high-oblique-far-shell-off --cloud-camera-mode high-oblique \
    --cloud-far-shell-strength 0
capture high-oblique-far-shell-strong --cloud-camera-mode high-oblique \
    --cloud-far-shell-strength 1.5
capture surface --cloud-camera-mode surface
capture high --cloud-camera-mode high
capture orbit-satellite-preview --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell
capture orbit-satellite-fill-off --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --cloud-orbit-fill 0
capture orbit-satellite-fill-strong --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --cloud-orbit-fill 1.5
capture orbit-satellite-high-oblique --cloud-camera-mode high-oblique \
    --cloud-orbit-representation surface-shell
capture orbit-volume-comparison --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation volume
capture orbit-satellite-detail-off --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --cloud-orbit-detail-strength 0
capture orbit-satellite-detail-strong --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --cloud-orbit-detail-strength 0.8
capture orbit-satellite-terminator --cloud-camera-mode orbit-terminator \
    --cloud-distance-mode orbit-shell --cloud-orbit-representation surface-shell
capture orbit-satellite-shell-envelope --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --debug-view orbit-envelope
capture orbit-satellite-shell-alpha --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --debug-view orbit-shell-alpha
capture orbit-satellite-shell-height --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --debug-view orbit-shell-height
capture orbit-satellite-shell-normal --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --debug-view orbit-shell-normal
capture orbit-satellite-shell-shadow --cloud-camera-mode orbit --cloud-distance-mode orbit-shell \
    --cloud-orbit-representation surface-shell --debug-view orbit-shell-shadow
capture high-oblique-distance-regime --cloud-camera-mode high-oblique \
    --debug-view distance-regime
capture high-oblique-local-alpha --cloud-camera-mode high-oblique --debug-view local-alpha
capture high-oblique-orbit-alpha --cloud-camera-mode high-oblique --debug-view orbit-alpha
capture high-oblique-orbit-ray-coverage --cloud-camera-mode high-oblique \
    --debug-view orbit-coverage
capture high-oblique-orbit-ray-detail --cloud-camera-mode high-oblique --debug-view orbit-detail
capture high-oblique-orbit-ray-hull --cloud-camera-mode high-oblique --debug-view orbit-hull
capture orbit-distance-regime --cloud-camera-mode orbit --debug-view distance-regime
capture orbit-alpha --cloud-camera-mode orbit --debug-view orbit-alpha
capture orbit-ray-coverage --cloud-camera-mode orbit --debug-view orbit-coverage
capture orbit-ray-detail --cloud-camera-mode orbit --debug-view orbit-detail
capture orbit-ray-hull --cloud-camera-mode orbit --debug-view orbit-hull
capture high-oblique-authored-weather --cloud-camera-mode high-oblique \
    --debug-view authored-weather
capture high-oblique-coverage-bias --cloud-camera-mode high-oblique \
    --debug-view coverage-bias
capture raw-final --cloud-camera-mode surface-up --debug-view raw-final
capture authored-weather --cloud-camera-mode surface-up --debug-view authored-weather
capture local-scatter --cloud-camera-mode surface-up --debug-view local-scatter
capture weather-edge --cloud-camera-mode surface-up --debug-view weather-edge
capture coverage-bias --cloud-camera-mode surface-up --debug-view coverage-bias
capture base-density --cloud-camera-mode surface-up --debug-view base-density
capture detail-density --cloud-camera-mode surface-up --debug-view detail-density
capture cloud-type --cloud-camera-mode surface-up --debug-view cloud-type
capture density --cloud-camera-mode surface-up --debug-view density
capture visible-density --cloud-camera-mode surface-up --debug-view visible-density
capture visible-cloud-type --cloud-camera-mode surface-up --debug-view visible-cloud-type
capture cloud-alpha --cloud-camera-mode surface-up --debug-view cloud-alpha
capture local-alpha --cloud-camera-mode surface-up --debug-view local-alpha
capture orbit-alpha-surface-up --cloud-camera-mode surface-up --debug-view orbit-alpha
capture distance-regime --cloud-camera-mode surface-up --debug-view distance-regime

if [[ "${DEEP}" != "0" ]]; then
    motion_video="${OUT_DIR}/orbit-satellite-motion.mp4"
    "${APP}" \
        --headless \
        --capture video \
        --frames "${MOTION_FRAMES}" \
        --fps "${MOTION_FPS}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        --cloud-camera-mode orbit \
        --cloud-distance-mode orbit-shell \
        --cloud-orbit-representation surface-shell \
        --cloud-orbit-motion-strength 4 \
        --cloud-wind-speed-mps 900 \
        --output "${motion_video}"
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -y -loglevel error -i "${motion_video}" -frames:v 1 \
            "${OUT_DIR}/orbit-satellite-motion-start.png"
        ffmpeg -y -loglevel error -sseof -0.1 -i "${motion_video}" -frames:v 1 \
            "${OUT_DIR}/orbit-satellite-motion-later.png"
    fi
    capture orbit-local-weather --cloud-camera-mode orbit --debug-view weather
    capture orbit-local-coverage-bias --cloud-camera-mode orbit --debug-view coverage-bias
    capture surface-up-weather-local --cloud-camera-mode surface-up --cloud-weather-influence 0
    capture surface-up-weather-authored --cloud-camera-mode surface-up --cloud-weather-influence 1
    capture high-oblique-weather-local --cloud-camera-mode high-oblique --cloud-weather-influence 0
    capture high-oblique-weather-authored --cloud-camera-mode high-oblique \
        --cloud-weather-influence 1
    capture surface-up-bayer --cloud-camera-mode surface-up --cloud-sampling-mode bayer
    capture surface-up-no-jitter --cloud-camera-mode surface-up --cloud-sampling-mode off
    capture high-oblique-bayer --cloud-camera-mode high-oblique --cloud-sampling-mode bayer
    capture high-oblique-no-jitter --cloud-camera-mode high-oblique --cloud-sampling-mode off
    capture raw-final-bayer --cloud-camera-mode surface-up --debug-view raw-final \
        --cloud-sampling-mode bayer
    capture raw-final-no-jitter --cloud-camera-mode surface-up --debug-view raw-final \
        --cloud-sampling-mode off
    capture transmittance --cloud-camera-mode surface-up --debug-view transmittance
    capture lighting --cloud-camera-mode surface-up --debug-view lighting
    capture ambient-light --cloud-camera-mode surface-up --debug-view ambient-light
    capture direct-light --cloud-camera-mode surface-up --debug-view direct-light
    capture phase-light --cloud-camera-mode surface-up --debug-view phase-light
    capture shadow --cloud-camera-mode surface-up --debug-view shadow
    capture distance --cloud-camera-mode surface-up --debug-view distance
    capture metadata-distance --cloud-camera-mode surface-up --debug-view metadata-distance
    capture metadata-alpha --cloud-camera-mode surface-up --debug-view metadata-alpha
    capture metadata-confidence --cloud-camera-mode surface-up --debug-view metadata-confidence
    capture metadata-density --cloud-camera-mode surface-up --debug-view metadata-density
    capture steps --cloud-camera-mode surface-up --debug-view steps
    capture background --cloud-camera-mode surface-up --debug-view background
fi

if command -v magick >/dev/null 2>&1; then
    rm -f "${OUT_DIR}/contact-sheet.png"
    magick montage "${OUT_DIR}"/*.png -geometry 320x180+8+8 -tile 2x \
        "${OUT_DIR}/contact-sheet.png"
    crop_dir="${OUT_DIR}/diagnostic-crops"
    mkdir -p "${crop_dir}"
    crop_inputs=()
    crop_names=(
        surface-up raw-final cloud-alpha authored-weather local-scatter weather-edge coverage-bias
        cloud-type density visible-density visible-cloud-type
        high-oblique-far-shell-off high-oblique-far-shell-strong
        orbit-satellite-preview orbit-satellite-high-oblique orbit-volume-comparison
        orbit-satellite-fill-off orbit-satellite-fill-strong
        orbit-satellite-detail-off orbit-satellite-detail-strong
        orbit-satellite-terminator orbit-satellite-shell-envelope
        orbit-satellite-shell-alpha orbit-satellite-shell-height
        orbit-satellite-shell-normal orbit-satellite-shell-shadow
        high-oblique-distance-regime high-oblique-local-alpha high-oblique-orbit-alpha
        high-oblique-orbit-ray-coverage high-oblique-orbit-ray-detail
        high-oblique-orbit-ray-hull
        orbit-distance-regime orbit-alpha orbit-ray-coverage orbit-ray-detail orbit-ray-hull
    )
    if [[ "${DEEP}" != "0" ]]; then
        crop_names+=(
            orbit-satellite-motion-start orbit-satellite-motion-later
            orbit-local-weather orbit-local-coverage-bias
            metadata-alpha metadata-distance metadata-confidence metadata-density
            transmittance lighting ambient-light direct-light phase-light shadow
            steps background
        )
    fi
    for name in "${crop_names[@]}"; do
        if [[ -f "${OUT_DIR}/${name}.png" ]]; then
            crop_path="${crop_dir}/${name}-center.png"
            magick "${OUT_DIR}/${name}.png" -crop "${CENTER_CROP_GEOMETRY}" \
                -resize 1560x990 "${crop_path}"
            crop_inputs+=("${crop_path}")
        fi
    done
    if (( ${#crop_inputs[@]} > 0 )); then
        magick montage "${crop_inputs[@]}" -geometry 390x248+6+6 -tile 2x \
            "${crop_dir}/center-feature-contact.png"
    fi
fi

printf 'cloud captures written to %s\n' "${OUT_DIR}"
