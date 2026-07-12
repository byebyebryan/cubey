#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean-sea-states-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
FOAM_FRAMES="${FOAM_FRAMES:-120}"
MAP_SIZE="${MAP_SIZE:-512}"
QUALITY="${QUALITY:-full}"
WEATHER_PRESET="${WEATHER_PRESET:-surface-volume}"
MOTION="${MOTION:-1}"
MOTION_WIDTH="${MOTION_WIDTH:-960}"
MOTION_HEIGHT="${MOTION_HEIGHT:-540}"
MOTION_FRAMES="${MOTION_FRAMES:-120}"
MOTION_FPS="${MOTION_FPS:-30}"

if [[ "${MOTION}" != "0" && "${MOTION}" != "1" ]]; then
    printf 'MOTION must be 0 or 1\n' >&2
    exit 2
fi

mkdir -p "${OUT_DIR}"

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()
SEA_STATES=(calm windy stormy)

printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
{
    printf '# Ocean Sea-State Review\n\n'
    printf -- '- Still size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Still frames: %s\n' "${FRAMES}"
    printf -- '- Foam history warmup frames: %s\n' "${FOAM_FRAMES}"
    printf -- '- Ocean map size: %s\n' "${MAP_SIZE}"
    printf -- '- Cloud quality: %s\n' "${QUALITY}"
    printf -- '- Weather preset: %s\n\n' "${WEATHER_PRESET}"
    printf 'All rows keep C0/C1, domains, directions, seeds, and quality fixed. '
    printf 'No-cloud rows isolate the water model; environment rows verify shared lighting.\n\n'
    printf '| Capture | Group | Args |\n'
    printf '|---|---|---|\n'
} >"${INDEX}"

capture_named_frames() {
    local capture_frames="$1"
    local name="$2"
    local title="$3"
    local group="$4"
    shift 4

    "${APP}" \
        --headless \
        --frames "${capture_frames}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --ocean-map-size "${MAP_SIZE}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${WEATHER_PRESET}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="$*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" \
        >>"${MANIFEST}"
    printf '| [%s](%s.png) | %s | `%s` |\n' "${title}" "${name}" "${group}" "${args}" \
        >>"${INDEX}"
}

capture_named() {
    capture_named_frames "${FRAMES}" "$@"
}

capture_state_row_frames() {
    local capture_frames="$1"
    local suffix="$2"
    local label="$3"
    local group="$4"
    shift 4

    for state in "${SEA_STATES[@]}"; do
        capture_named_frames "${capture_frames}" "${state}-${suffix}" \
            "${state^} / ${label}" "${group}" --ocean-sea-state "${state}" "$@"
    done
}

capture_state_row() {
    capture_state_row_frames "${FRAMES}" "$@"
}

noon_base=(
    --time-of-day-mode manual
    --sun-elevation 42
    --sun-azimuth -20
    --pause-time
)
twilight_base=(
    --time-of-day-mode solar
    --time-hours 18.2
    --day-of-year 80
    --latitude-degrees 30
    --sun-azimuth-offset 90
    --pause-time
)

capture_state_row low-clear "low / clear" scale --ocean-camera-preset low --no-clouds \
    "${noon_base[@]}"
capture_state_row mid-clear "mid / clear" scale --ocean-camera-preset mid --no-clouds \
    "${noon_base[@]}"
capture_state_row high-clear "high / clear" scale --ocean-camera-preset high --no-clouds \
    "${noon_base[@]}"
capture_state_row mid-clouds "mid / clouds" environment --ocean-camera-preset mid \
    "${noon_base[@]}"
capture_state_row twilight twilight environment --ocean-camera-preset mid \
    "${twilight_base[@]}"
capture_state_row displacement displacement diagnostics --ocean-camera-preset low --no-clouds \
    --debug-view displacement "${noon_base[@]}"
capture_state_row_frames "${FOAM_FRAMES}" foam foam diagnostics --ocean-camera-preset low \
    --no-clouds --debug-view foam "${noon_base[@]}"

if command -v magick >/dev/null 2>&1; then
    montage_inputs=()
    for index in "${!CAPTURE_FILES[@]}"; do
        montage_inputs+=("-label" "${CAPTURE_LABELS[${index}]}" "${CAPTURE_FILES[${index}]}")
    done
    magick montage "${montage_inputs[@]}" -geometry 320x180+8+26 -tile 3x \
        "${OUT_DIR}/contact-sheet.png"
fi

if [[ "${MOTION}" == "1" ]]; then
    {
        printf '\n## Motion\n\n'
        printf 'Fixed-lighting motion checks use the mid camera with clouds disabled.\n\n'
    } >>"${INDEX}"
    for state in "${SEA_STATES[@]}"; do
        motion_file="${state}-motion.mp4"
        "${APP}" \
            --headless \
            --capture video \
            --frames "${MOTION_FRAMES}" \
            --fps "${MOTION_FPS}" \
            --width "${MOTION_WIDTH}" \
            --height "${MOTION_HEIGHT}" \
            --ocean-map-size "${MAP_SIZE}" \
            --ocean-sea-state "${state}" \
            --ocean-camera-preset mid \
            --no-clouds \
            "${noon_base[@]}" \
            --output "${OUT_DIR}/${motion_file}"
        printf -- '- [%s motion](%s)\n' "${state^}" "${motion_file}" >>"${INDEX}"
    done
fi

printf 'Ocean sea-state review written to %s\n' "${OUT_DIR}"
