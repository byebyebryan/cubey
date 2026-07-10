#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ATMOSPHERE_APP="${ATMOSPHERE_APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
PLANET_APP="${PLANET_APP:-${ROOT_DIR}/build/dev/projects/planet/planet}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-star-field-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
FRAMES="${FRAMES:-2}"
DEEP="${DEEP:-0}"

mkdir -p "${OUT_DIR}"

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
{
    printf '# Star Field Review\n\n'
    printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Frames: %s\n\n' "${FRAMES}"
    printf 'The atmosphere rows use the isolated `stars` view. Planet rows disable '
    printf 'clouds, moon, and Milky Way to check final-scene integration.\n\n'
    printf '| Capture | Group | Args |\n'
    printf '|---|---|---|\n'
} >"${INDEX}"

record_capture() {
    local app="$1"
    local name="$2"
    local title="$3"
    local group="$4"
    shift 4

    "${app}" --headless --frames "${FRAMES}" --width "${WIDTH}" --height "${HEIGHT}" \
        "$@" --output "${OUT_DIR}/${name}.png"
    local args="$*"
    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s.png) | %s | `%s` |\n' "${title}" "${name}" "${group}" "${args}" \
        >>"${INDEX}"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
}

atmosphere_base=(
    --atmosphere-preset night
    --pause-time
    --debug-view stars
    --no-clouds
    --no-reference-geometry
    --milky-way-intensity 0
)

record_capture "${ATMOSPHERE_APP}" surface-horizon-human "Surface horizon / human" surface \
    "${atmosphere_base[@]}" --night-sky-mode human --camera-pitch-offset-deg 0
record_capture "${ATMOSPHERE_APP}" surface-zenith-human "Surface zenith / human" surface \
    "${atmosphere_base[@]}" --night-sky-mode human --camera-pitch-offset-deg 65
record_capture "${ATMOSPHERE_APP}" surface-zenith-camera "Surface zenith / camera" response \
    "${atmosphere_base[@]}" --night-sky-mode camera --camera-pitch-offset-deg 65
record_capture "${ATMOSPHERE_APP}" surface-pollution "Surface light pollution" response \
    "${atmosphere_base[@]}" --night-sky-mode human --camera-pitch-offset-deg 35 \
    --light-pollution 0.75
record_capture "${ATMOSPHERE_APP}" surface-moon-washout "Surface moon washout" response \
    "${atmosphere_base[@]}" --night-sky-mode human --camera-pitch-offset-deg 35 \
    --moon --moon-intensity 1.5 --moonlight-intensity 1.5

for yaw in 0 45 90 135 180; do
    record_capture "${ATMOSPHERE_APP}" "yaw-${yaw}" "Yaw ${yaw} deg" yaw \
        "${atmosphere_base[@]}" --night-sky-mode human --camera-pitch-offset-deg 35 \
        --camera-yaw-offset-deg "${yaw}"
done

planet_base=(
    --planet-pause-time
    --no-clouds
    --no-moon
    --milky-way-intensity 0
    --star-intensity 1.0
    --star-density 0.65
)
record_capture "${PLANET_APP}" planet-surface "Planet surface" integration \
    "${planet_base[@]}" --planet-camera-mode surface --planet-camera-altitude-m 1200 \
    --planet-day-of-year 80 --planet-time-hours 12 --planet-camera-surface-look antisun \
    --planet-camera-surface-pitch-deg 35
record_capture "${PLANET_APP}" planet-orbit "Planet orbit" integration \
    "${planet_base[@]}" --planet-camera-mode orbit --planet-camera-altitude-m 14000000 \
    --planet-day-of-year 80 --planet-time-hours 12

if command -v magick >/dev/null 2>&1; then
    montage_inputs=()
    for index in "${!CAPTURE_FILES[@]}"; do
        montage_inputs+=("-label" "${CAPTURE_LABELS[${index}]}" "${CAPTURE_FILES[${index}]}")
    done
    magick montage "${montage_inputs[@]}" -geometry 384x216+8+26 -tile 4x \
        "${OUT_DIR}/contact-sheet.png"
fi

if [[ "${DEEP}" == "1" ]]; then
    "${ATMOSPHERE_APP}" --headless --capture video --frames 240 --width "${WIDTH}" \
        --height "${HEIGHT}" --atmosphere-preset night --debug-view stars --no-clouds \
        --no-reference-geometry --milky-way-intensity 0 --night-sky-mode human \
        --time-speed-hours-per-second 0.10 --output "${OUT_DIR}/sidereal-motion.mp4"
fi

printf 'Wrote %s\n' "${OUT_DIR}"
