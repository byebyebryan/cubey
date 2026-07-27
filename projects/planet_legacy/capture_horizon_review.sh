#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/planet_legacy/planet-legacy}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/planet-legacy-horizon-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-quarter}"
PRESET="${PRESET:-broken-cumulus}"

mkdir -p "${OUT_DIR}"

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

base_args=(
    --headless
    --frames "${FRAMES}"
    --width "${WIDTH}"
    --height "${HEIGHT}"
    --planet-pause-time
)

capture() {
    local name="$1"
    shift
    "${APP}" "${base_args[@]}" "$@" --output "${OUT_DIR}/${name}.png"
}

capture_clouds() {
    local name="$1"
    shift
    capture "${name}" \
        --clouds \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        "$@"
}

write_index_header() {
    printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
    {
        printf '# Planet Horizon Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Cloud quality: %s\n' "${QUALITY}"
        printf -- '- Weather preset: %s\n\n' "${PRESET}"
        printf '| Capture | Group | Args |\n'
        printf '|---|---|---|\n'
    } >"${INDEX}"
}

capture_named() {
    local name="$1"
    local title="$2"
    local group="$3"
    shift 3

    capture "${name}" "$@"

    local args="$*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | `%s` |\n' "${title}" "${name}.png" "${group}" "${args}" >>"${INDEX}"
}

capture_clouds_named() {
    local name="$1"
    local title="$2"
    local group="$3"
    shift 3

    capture_clouds "${name}" "$@"

    local args="--clouds --cloud-quality ${QUALITY} --cloud-weather-preset ${PRESET} $*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | `%s` |\n' "${title}" "${name}.png" "${group}" "${args}" >>"${INDEX}"
}

write_contact_sheet() {
    if ! command -v magick >/dev/null 2>&1; then
        return
    fi

    rm -f "${OUT_DIR}/contact-sheet.png"
    local montage_inputs=()
    local index
    for index in "${!CAPTURE_FILES[@]}"; do
        montage_inputs+=("-label" "${CAPTURE_LABELS[${index}]}" "${CAPTURE_FILES[${index}]}")
    done
    magick montage "${montage_inputs[@]}" -geometry 320x180+8+26 -tile 3x \
        "${OUT_DIR}/contact-sheet.png"
}

surface_dawn=(
    --planet-camera-mode surface
    --planet-camera-altitude-m 1200
    --planet-day-of-year 80
    --planet-time-hours 4.75
    --planet-camera-surface-look sun
    --planet-camera-surface-pitch-deg 12
)
surface_day=(
    --planet-camera-mode surface
    --planet-camera-altitude-m 1200
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --planet-camera-surface-look sun
    --planet-camera-surface-pitch-deg 6
)
surface_night=(
    --planet-camera-mode surface
    --planet-camera-altitude-m 1200
    --planet-day-of-year 80
    --planet-time-hours 0.0
    --planet-camera-surface-pitch-deg 8
)
high_060=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 60000
    --planet-day-of-year 80
    --planet-time-hours 12.0
)
high_140=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 140000
    --planet-day-of-year 80
    --planet-time-hours 12.0
)
high_260=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 260000
    --planet-day-of-year 80
    --planet-time-hours 12.0
)

write_index_header

capture_named surface-dawn-no-clouds "Surface dawn no clouds" no-clouds \
    "${surface_dawn[@]}"
capture_named surface-day-no-clouds "Surface day no clouds" no-clouds \
    "${surface_day[@]}"
capture_named surface-night-no-clouds "Surface night no clouds" no-clouds \
    "${surface_night[@]}"
capture_named high-060-no-clouds "High 60km no clouds" no-clouds \
    "${high_060[@]}"
capture_named high-140-no-clouds "High 140km no clouds" no-clouds \
    "${high_140[@]}"
capture_named high-260-no-clouds "High 260km no clouds" no-clouds \
    "${high_260[@]}"

capture_named surface-day-background "Surface day sky/background" sky \
    "${surface_day[@]}" --clouds --cloud-quality "${QUALITY}" --cloud-debug-view background
capture_named high-140-background "High 140km sky/background" sky \
    "${high_140[@]}" --clouds --cloud-quality "${QUALITY}" --cloud-debug-view background

capture_clouds_named surface-day-clouds "Surface day clouds" clouds \
    "${surface_day[@]}"
capture_clouds_named high-060-clouds "High 60km clouds" clouds \
    "${high_060[@]}"
capture_clouds_named high-140-clouds "High 140km clouds" clouds \
    "${high_140[@]}"
capture_clouds_named high-260-clouds "High 260km clouds" clouds \
    "${high_260[@]}"
capture_clouds_named high-140-depth-occlusion "High 140km depth occlusion" diagnostics \
    "${high_140[@]}" --cloud-debug-view scene-depth-occlusion

write_contact_sheet

printf 'Wrote %s\n' "${OUT_DIR}"
