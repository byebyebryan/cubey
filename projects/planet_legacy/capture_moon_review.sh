#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/planet_legacy/planet-legacy}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/planet-legacy-moon-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"

mkdir -p "${OUT_DIR}"

if [[ ! -x "${APP}" ]]; then
    printf 'missing planet app: %s\n' "${APP}" >&2
    exit 1
fi

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

write_header() {
    printf 'file\tgroup\targs\n' >"${MANIFEST}"
    {
        printf '# Planet Moon Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Clouds: disabled\n\n'
        printf 'This pack checks final-scene integration of the shared geometry moon path '
        printf 'inside planet using the protected day-moon and occlusion presets. The additional '
        printf 'environment rows complement the atmosphere phase and material diagnostics.\n\n'
        printf '| Capture | Group | Args |\n'
        printf '|---|---|---|\n'
    } >"${INDEX}"
}

capture_named() {
    local name="$1"
    local title="$2"
    local group="$3"
    shift 3

    "${APP}" "${base_args[@]}" "$@" --output "${OUT_DIR}/${name}.png"

    local args="--no-clouds $*"
    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\n' "${name}.png" "${group}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | `%s` |\n' "${title}" "${name}.png" "${group}" "${args}" \
        >>"${INDEX}"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
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

surface_base=(
    --planet-camera-mode surface
)

orbit_base=(
    --planet-camera-mode orbit
)

write_header

capture_named surface-day-moon "Surface day moon" surface \
    "${surface_base[@]}" \
    --planet-day-of-year 87.4 \
    --planet-time-hours 12.0

capture_named orbit-moon-occlusion "Orbit moon occlusion" orbit \
    "${orbit_base[@]}" \
    --planet-day-of-year 88 \
    --planet-time-hours 18.13

capture_named surface-antisun-night "Surface antisun night" environment \
    "${surface_base[@]}" \
    --planet-camera-altitude-m 1200 \
    --planet-day-of-year 80 \
    --planet-time-hours 12.0 \
    --planet-camera-surface-look antisun \
    --planet-camera-surface-pitch-deg 12

capture_named surface-twilight "Surface twilight" environment \
    "${surface_base[@]}" \
    --planet-camera-altitude-m 1200 \
    --planet-day-of-year 80 \
    --planet-time-hours 17.8 \
    --planet-camera-surface-look antisun \
    --planet-camera-surface-pitch-deg 8

capture_named orbit-starfield "Orbit starfield" environment \
    "${orbit_base[@]}" \
    --planet-camera-altitude-m 14000000 \
    --planet-day-of-year 80 \
    --planet-time-hours 12.0

write_contact_sheet

printf 'planet moon review captures written to %s\n' "${OUT_DIR}"
