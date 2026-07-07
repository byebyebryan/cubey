#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/planet/planet}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/planet-sky-foundation-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"

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
    --no-clouds
)

capture() {
    local name="$1"
    shift
    "${APP}" "${base_args[@]}" "$@" --output "${OUT_DIR}/${name}.png"
}

write_index_header() {
    printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
    {
        printf '# Planet Sky Foundation Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Clouds: disabled\n\n'
        printf 'This pack reviews planet use of the shared atmosphere/sky foundation. '
        printf 'Cloud aerial/orbit quality is intentionally out of scope.\n\n'
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

    local args="--no-clouds $*"
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
    magick montage "${montage_inputs[@]}" -geometry 320x180+8+26 -tile 4x \
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
    --planet-time-hours 18.0
    --planet-camera-surface-look sun
    --planet-camera-surface-pitch-deg 8
)
surface_night=(
    --planet-camera-mode surface
    --planet-camera-altitude-m 1200
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --planet-camera-surface-pitch-deg 12
)
high_060=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 60000
    --planet-day-of-year 80
    --planet-time-hours 18.0
)
high_140=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 140000
    --planet-day-of-year 80
    --planet-time-hours 18.0
)
orbit_lit=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 2400000
    --planet-day-of-year 80
    --planet-time-hours 0.0
)
orbit_terminator=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 2400000
    --planet-day-of-year 80
    --planet-time-hours 15.0
)
orbit_starfield=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 2400000
    --planet-day-of-year 112
    --planet-time-hours 12.0
)
orbit_sun_glare=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 2400000
    --planet-day-of-year 80
    --planet-time-hours 6.0
)

write_index_header

capture_named surface-dawn "Surface dawn" surface "${surface_dawn[@]}"
capture_named surface-day "Surface day" surface "${surface_day[@]}"
capture_named surface-night "Surface night" surface "${surface_night[@]}"
capture_named surface-day-yaw-left "Surface yaw left" surface \
    "${surface_day[@]}" --planet-camera-surface-yaw-deg -45
capture_named surface-day-yaw-right "Surface yaw right" surface \
    "${surface_day[@]}" --planet-camera-surface-yaw-deg 45

capture_named high-060 "High 60km" high "${high_060[@]}"
capture_named high-140 "High 140km" high "${high_140[@]}"

capture_named orbit-lit "Orbit lit" orbit "${orbit_lit[@]}"
capture_named orbit-terminator "Orbit terminator" orbit "${orbit_terminator[@]}"
capture_named orbit-starfield "Orbit starfield" orbit "${orbit_starfield[@]}"
capture_named orbit-sun-glare "Orbit sun limb glow" orbit "${orbit_sun_glare[@]}"

write_contact_sheet

printf 'Wrote %s\n' "${OUT_DIR}"
