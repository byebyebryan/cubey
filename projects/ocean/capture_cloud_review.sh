#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean-cloud-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
MAP_SIZE="${MAP_SIZE:-512}"
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
        printf '# Ocean Cloud Lighting Review\n\n'
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

noon_review_view=(
    --time-of-day-mode solar
    --time-hours 12.0
    --day-of-year 172
    --latitude-degrees 30
    --ocean-camera-preset mid
)
noon_near_view=(
    --time-of-day-mode solar
    --time-hours 12.0
    --day-of-year 172
    --latitude-degrees 30
    --ocean-camera-preset default
)
noon_high_view=(
    --time-of-day-mode solar
    --time-hours 12.0
    --day-of-year 172
    --latitude-degrees 30
    --ocean-camera-preset high
)
sunset_view=(
    --time-of-day-mode solar
    --time-hours 17.8
    --day-of-year 80
    --latitude-degrees 30
    --ocean-camera-preset mid
)
night_view=(
    --time-of-day-mode solar
    --time-hours 0.0
    --day-of-year 80
    --latitude-degrees 30
    --ocean-camera-preset mid
)

write_index_header

capture_named noon-clouds "Noon clouds" final \
    "${noon_review_view[@]}"
capture_named noon-no-clouds "Noon no clouds" comparison \
    "${noon_review_view[@]}" --no-clouds
capture_named noon-near-stress "Noon near stress view" scale \
    "${noon_near_view[@]}"
capture_named noon-reflection-off "Noon reflection coupling off" reflection \
    "${noon_review_view[@]}" --debug-view reflection \
    --ocean-cloud-reflection-strength 0.0
capture_named noon-reflection-on "Noon reflection coupling on" reflection \
    "${noon_review_view[@]}" --debug-view reflection \
    --ocean-cloud-reflection-strength 0.75
capture_named noon-cloud-reflection "Noon cloud reflection" reflection \
    "${noon_review_view[@]}" --debug-view cloud-reflection \
    --ocean-cloud-reflection-strength 0.75
capture_named noon-shadow-map "Noon cloud transmittance" shadow \
    "${noon_review_view[@]}" --debug-view cloud-shadow --cloud-coverage 0.75
capture_named noon-direct-shadow-off "Noon direct light without cloud shadow" shadow \
    "${noon_review_view[@]}" --debug-view direct-light \
    --cloud-coverage 0.75 --ocean-cloud-shadow-strength 0.0
capture_named noon-direct-shadow-on "Noon direct light with cloud shadow" shadow \
    "${noon_review_view[@]}" --debug-view direct-light \
    --cloud-coverage 0.75 --ocean-cloud-shadow-strength 0.45
capture_named high-reflection "High cloud reflection" scale \
    "${noon_high_view[@]}" --debug-view reflection \
    --ocean-cloud-reflection-strength 0.75
capture_named sunset-clouds "Sunset clouds" lighting \
    "${sunset_view[@]}"
capture_named sunset-reflection "Sunset cloud reflection" lighting \
    "${sunset_view[@]}" --debug-view reflection \
    --ocean-cloud-reflection-strength 0.75
capture_named night-clouds "Night clouds" lighting \
    "${night_view[@]}"
capture_named night-reflection "Night cloud reflection" lighting \
    "${night_view[@]}" --debug-view reflection \
    --ocean-cloud-reflection-strength 0.75
capture_named noon-cloud-alpha "Noon cloud alpha" diagnostics \
    "${noon_review_view[@]}" --cloud-debug-view cloud-alpha
capture_named noon-cloud-density "Noon cloud density" diagnostics \
    "${noon_review_view[@]}" --cloud-debug-view density
capture_named noon-scene-depth "Noon scene depth occlusion" diagnostics \
    "${noon_review_view[@]}" --cloud-debug-view scene-depth-occlusion

write_contact_sheet

printf 'Ocean cloud review captures written to %s\n' "${OUT_DIR}"
