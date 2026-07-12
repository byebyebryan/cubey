#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean-spectral-handoff-review}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-240}"
FPS="${FPS:-60}"

command -v ffmpeg >/dev/null 2>&1 || {
    printf 'ffmpeg is required to extract warmed review frames\n' >&2
    exit 2
}
mkdir -p "${OUT_DIR}"

base_args=(
    --headless
    --capture video
    --frames "${FRAMES}"
    --fps "${FPS}"
    --width "${WIDTH}"
    --height "${HEIGHT}"
    --ocean-camera-preset high
    --no-clouds
    --time-of-day-mode manual
    --sun-elevation 42
    --sun-azimuth -20
    --pause-time
    --no-validation
)

capture_warmed() {
    local name="$1"
    local state="$2"
    local handoff="$3"
    local debug_view="${4:-final}"
    local video="${OUT_DIR}/${name}.mp4"
    local image="${OUT_DIR}/${name}.png"
    local debug_args=()
    if [[ "${debug_view}" != "final" ]]; then
        debug_args=(--debug-view "${debug_view}")
    fi
    "${APP}" "${base_args[@]}" \
        --ocean-sea-state "${state}" \
        --ocean-spectral-lod-handoff "${handoff}" \
        "${debug_args[@]}" \
        --output "${video}"
    ffmpeg -hide_banner -loglevel error -y -sseof -0.05 -i "${video}" \
        -frames:v 1 "${image}"
}

for state in calm windy stormy; do
    capture_warmed "${state}-off" "${state}" 0
    capture_warmed "${state}-on" "${state}" 1
done
capture_warmed slope-lod windy 1 slope-lod
capture_warmed foam-lod windy 1 foam-lod

if command -v magick >/dev/null 2>&1; then
    magick montage \
        -label 'Calm / off' "${OUT_DIR}/calm-off.png" \
        -label 'Calm / on' "${OUT_DIR}/calm-on.png" \
        -label 'Windy / off' "${OUT_DIR}/windy-off.png" \
        -label 'Windy / on' "${OUT_DIR}/windy-on.png" \
        -label 'Stormy / off' "${OUT_DIR}/stormy-off.png" \
        -label 'Stormy / on' "${OUT_DIR}/stormy-on.png" \
        -geometry 480x270+4+24 -tile 2x3 "${OUT_DIR}/sea-state-compare.png"
    magick montage \
        -label 'Slope LOD: R slope / G roughness / B transfer' "${OUT_DIR}/slope-lod.png" \
        -label 'Foam LOD: R history / G current / B transfer' "${OUT_DIR}/foam-lod.png" \
        -geometry 640x360+4+24 -tile 1x2 "${OUT_DIR}/diagnostics.png"
fi

printf 'Ocean spectral handoff review written to %s\n' "${OUT_DIR}"
