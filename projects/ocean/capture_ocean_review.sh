#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean-closure-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-18}"
FOAM_FRAMES="${FOAM_FRAMES:-120}"
MAP_SIZE="${MAP_SIZE:-512}"
CLOUD_QUALITY="${CLOUD_QUALITY:-full}"
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
SOURCE_COMMIT="$(git -C "${ROOT_DIR}" rev-parse HEAD)"

printf 'file\ttitle\tgroup\tframes\targs\n' >"${MANIFEST}"
{
    printf '# Ocean Closure Review\n\n'
    printf -- '- Source: `%s`\n' "${SOURCE_COMMIT}"
    printf -- '- Still size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Ocean map: %s\n' "${MAP_SIZE}"
    printf -- '- Cloud quality / weather: %s / %s\n\n' \
        "${CLOUD_QUALITY}" "${WEATHER_PRESET}"
    printf '| Capture | Group | Frames | Args |\n'
    printf '|---|---|---:|---|\n'
} >"${INDEX}"

capture_named() {
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
        --cloud-quality "${CLOUD_QUALITY}" \
        --cloud-weather-preset "${WEATHER_PRESET}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="$*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" \
        "${capture_frames}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s.png) | %s | %s | `%s` |\n' "${title}" "${name}" "${group}" \
        "${capture_frames}" "${args}" >>"${INDEX}"
}

capture_state_scale() {
    local state="$1"
    local camera="$2"
    capture_named "${FRAMES}" "${state}-${camera}-clear-noon" \
        "${state^} / ${camera} / clear noon" scale \
        --ocean-sea-state "${state}" --ocean-camera-preset "${camera}" --no-clouds \
        "${NOON_MANUAL[@]}"
}

NOON_MANUAL=(
    --time-of-day-mode manual
    --sun-elevation 42
    --sun-azimuth -20
    --pause-time
)
DAWN_SOLAR=(
    --time-of-day-mode solar
    --time-hours 5.8
    --day-of-year 80
    --latitude-degrees 30
    --sun-azimuth-offset 90
    --pause-time
)
DUSK_SOLAR=(
    --time-of-day-mode solar
    --time-hours 18.2
    --day-of-year 80
    --latitude-degrees 30
    --sun-azimuth-offset 90
    --pause-time
)
NIGHT_SOLAR=(
    --time-of-day-mode solar
    --time-hours 0.0
    --day-of-year 80
    --latitude-degrees 30
    --pause-time
)

for state in "${SEA_STATES[@]}"; do
    for camera in low mid high; do
        capture_state_scale "${state}" "${camera}"
    done
done

for camera in close mid high wide; do
    capture_named "${FRAMES}" "windy-${camera}-clouds-noon" \
        "Windy / ${camera} / cloudy noon" environment \
        --ocean-sea-state windy --ocean-camera-preset "${camera}" "${NOON_MANUAL[@]}"
done

capture_named "${FRAMES}" windy-mid-clouds-dawn "Windy / mid / dawn" lighting \
    --ocean-sea-state windy --ocean-camera-preset mid "${DAWN_SOLAR[@]}"
capture_named "${FRAMES}" windy-mid-clouds-dusk "Windy / mid / dusk" lighting \
    --ocean-sea-state windy --ocean-camera-preset mid "${DUSK_SOLAR[@]}"
capture_named "${FRAMES}" windy-mid-clouds-night "Windy / mid / night" lighting \
    --ocean-sea-state windy --ocean-camera-preset mid "${NIGHT_SOLAR[@]}"

capture_named "${FRAMES}" windy-close-specular "Windy / close / specular" diagnostics \
    --ocean-sea-state windy --ocean-camera-preset close --debug-view specular \
    "${DUSK_SOLAR[@]}"
capture_named "${FRAMES}" windy-mid-reflection "Windy / mid / reflection" diagnostics \
    --ocean-sea-state windy --ocean-camera-preset mid --debug-view reflection \
    "${DUSK_SOLAR[@]}"
capture_named "${FRAMES}" windy-mid-cloud-shadow "Windy / mid / cloud shadow" diagnostics \
    --ocean-sea-state windy --ocean-camera-preset mid --debug-view cloud-shadow \
    "${NOON_MANUAL[@]}"
capture_named "${FRAMES}" windy-high-lod "Windy / high / LOD" diagnostics \
    --ocean-sea-state windy --ocean-camera-preset high --no-clouds --debug-view lod \
    "${NOON_MANUAL[@]}"
capture_named "${FRAMES}" windy-wide-far-field "Windy / wide / far field" diagnostics \
    --ocean-sea-state windy --ocean-camera-preset wide --no-clouds --debug-view far-field \
    "${NOON_MANUAL[@]}"

for state in "${SEA_STATES[@]}"; do
    capture_named "${FOAM_FRAMES}" "${state}-low-foam" "${state^} / low / warmed foam" \
        diagnostics --ocean-sea-state "${state}" --ocean-camera-preset low --no-clouds \
        --debug-view foam "${NOON_MANUAL[@]}"
done

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
        printf 'Fixed-light motion uses the mid clear-water view; the Windy dusk clip also '
        printf 'checks moving clouds and low-sun lighting.\n\n'
    } >>"${INDEX}"
    for state in "${SEA_STATES[@]}"; do
        motion_file="${state}-mid-clear-motion.mp4"
        "${APP}" --headless --capture video --frames "${MOTION_FRAMES}" \
            --fps "${MOTION_FPS}" --width "${MOTION_WIDTH}" --height "${MOTION_HEIGHT}" \
            --ocean-map-size "${MAP_SIZE}" --ocean-sea-state "${state}" \
            --ocean-camera-preset mid --no-clouds "${NOON_MANUAL[@]}" \
            --output "${OUT_DIR}/${motion_file}"
        printf -- '- [%s mid clear motion](%s)\n' "${state^}" "${motion_file}" >>"${INDEX}"
    done
    "${APP}" --headless --capture video --frames "${MOTION_FRAMES}" \
        --fps "${MOTION_FPS}" --width "${MOTION_WIDTH}" --height "${MOTION_HEIGHT}" \
        --ocean-map-size "${MAP_SIZE}" --ocean-sea-state windy --ocean-camera-preset mid \
        "${DUSK_SOLAR[@]}" --output "${OUT_DIR}/windy-mid-clouds-dusk-motion.mp4"
    printf -- '- [Windy mid cloudy dusk motion](windy-mid-clouds-dusk-motion.mp4)\n' >>"${INDEX}"
fi

printf 'Ocean closure review written to %s\n' "${OUT_DIR}"
