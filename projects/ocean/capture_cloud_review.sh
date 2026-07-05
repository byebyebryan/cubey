#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean-cloud-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
MAP_SIZE="${MAP_SIZE:-128}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-surface-volume}"

mkdir -p "${OUT_DIR}"

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

capture() {
    local name="$1"
    shift
    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --ocean-map-size "${MAP_SIZE}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"
}

write_index_header() {
    printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
    {
        printf '# Ocean Cloud Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Ocean map size: %s\n' "${MAP_SIZE}"
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

default_view=(
    --ocean-camera-preset default
)
wide_view=(
    --ocean-camera-preset wide
)
sunset_view=(
    --time-of-day-mode solar
    --time-hours 17.8
    --day-of-year 80
    --latitude-degrees 30
    --ocean-camera-preset default
)
night_view=(
    --time-of-day-mode solar
    --time-hours 0.0
    --day-of-year 80
    --latitude-degrees 30
    --ocean-camera-preset default
)

write_index_header

capture_named default-clouds "Default clouds" final \
    "${default_view[@]}"
capture_named default-no-clouds "Default no clouds" comparison \
    "${default_view[@]}" --no-clouds
capture_named wide-clouds "Wide clouds" final \
    "${wide_view[@]}"
capture_named wide-no-clouds "Wide no clouds" comparison \
    "${wide_view[@]}" --no-clouds
capture_named sunset-clouds "Sunset clouds" lighting \
    "${sunset_view[@]}"
capture_named night-clouds "Night clouds" lighting \
    "${night_view[@]}"
capture_named default-cloud-alpha "Default cloud alpha" diagnostics \
    "${default_view[@]}" --cloud-debug-view cloud-alpha
capture_named default-cloud-density "Default cloud density" diagnostics \
    "${default_view[@]}" --cloud-debug-view density
capture_named default-scene-depth "Default scene depth occlusion" diagnostics \
    "${default_view[@]}" --cloud-debug-view scene-depth-occlusion

write_contact_sheet

printf 'Ocean cloud review captures written to %s\n' "${OUT_DIR}"
