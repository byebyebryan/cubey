#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/planet_legacy/planet-legacy}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/planet-legacy-cloud-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
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
        --planet-pause-time \
        --clouds \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"
}

write_index_header() {
    printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
    {
        printf '# Planet Cloud Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Quality: %s\n' "${QUALITY}"
        printf -- '- Weather preset: %s\n\n' "${PRESET}"
        printf 'Planet clouds are deferred pressure captures. Cloud V1 is surface-only '
        printf 'in atmosphere/ocean and should not be accepted from this pack.\n\n'
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

surface_sun_dawn=(
    --planet-camera-mode surface
    --planet-day-of-year 80
    --planet-time-hours 4.75
    --planet-camera-surface-look sun
    --planet-camera-surface-pitch-deg 22
)
surface_day=(
    --planet-camera-mode surface
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --planet-camera-surface-pitch-deg 32
)
surface_low_horizon=(
    --planet-camera-mode surface
    --planet-camera-altitude-m 1200
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --planet-camera-surface-look sun
    --planet-camera-surface-pitch-deg 6
)
surface_low_overhead=(
    --planet-camera-mode surface
    --planet-camera-altitude-m 1200
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --planet-camera-surface-look sun
    --planet-camera-surface-pitch-deg 42
)
surface_night=(
    --planet-camera-mode surface
    --planet-day-of-year 80
    --planet-time-hours 0.0
    --planet-camera-surface-pitch-deg 28
)
high_transition_60=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 60000
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --cloud-density-model experimental-aerial-orbit
    --cloud-distance-mode auto
)
high_transition_140=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 140000
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --cloud-density-model experimental-aerial-orbit
    --cloud-distance-mode auto
)
high_transition_260=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 260000
    --planet-day-of-year 80
    --planet-time-hours 12.0
    --cloud-density-model experimental-aerial-orbit
    --cloud-distance-mode auto
)
orbit_day=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 2400000
    --planet-day-of-year 80
    --planet-time-hours 18.0
    --cloud-density-model experimental-aerial-orbit
    --cloud-distance-mode auto
)
orbit_terminator=(
    --planet-camera-mode orbit
    --planet-camera-altitude-m 2400000
    --planet-day-of-year 80
    --planet-time-hours 16.0
    --cloud-density-model experimental-aerial-orbit
    --cloud-distance-mode auto
)

write_index_header

capture_named surface-dawn-clouds "Surface dawn clouds" surface \
    "${surface_sun_dawn[@]}"
capture_named surface-dawn-no-clouds "Surface dawn no clouds" surface \
    "${surface_sun_dawn[@]}" --no-clouds
capture_named surface-day-clouds "Surface day clouds" surface \
    "${surface_day[@]}"
capture_named surface-low-horizon-clouds "Surface low horizon clouds" surface \
    "${surface_low_horizon[@]}"
capture_named surface-low-overhead-clouds "Surface low overhead clouds" surface \
    "${surface_low_overhead[@]}"
capture_named surface-night-clouds "Surface night clouds" surface \
    "${surface_night[@]}"
capture_named surface-day-cloud-alpha "Surface day cloud alpha" diagnostics \
    "${surface_day[@]}" --cloud-debug-view cloud-alpha
capture_named surface-day-density "Surface day density" diagnostics \
    "${surface_day[@]}" --cloud-debug-view density
capture_named surface-day-local-structure "Surface day local structure" diagnostics \
    "${surface_day[@]}" --cloud-debug-view local-structure
capture_named surface-low-depth-occlusion "Surface low depth occlusion" diagnostics \
    "${surface_low_horizon[@]}" --cloud-debug-view scene-depth-occlusion

capture_named high-060-clouds "High 60km clouds" transition \
    "${high_transition_60[@]}"
capture_named high-060-no-clouds "High 60km no clouds" transition \
    "${high_transition_60[@]}" --no-clouds
capture_named high-060-distance "High 60km distance" transition \
    "${high_transition_60[@]}" --cloud-debug-view distance-regime
capture_named high-060-weights "High 60km weights" transition \
    "${high_transition_60[@]}" --cloud-debug-view transition-weights
capture_named high-140-clouds "High 140km clouds" transition \
    "${high_transition_140[@]}"
capture_named high-140-no-clouds "High 140km no clouds" transition \
    "${high_transition_140[@]}" --no-clouds
capture_named high-140-distance "High 140km distance" transition \
    "${high_transition_140[@]}" --cloud-debug-view distance-regime
capture_named high-140-weights "High 140km weights" transition \
    "${high_transition_140[@]}" --cloud-debug-view transition-weights
capture_named high-140-depth-occlusion "High 140km depth occlusion" transition \
    "${high_transition_140[@]}" --cloud-debug-view scene-depth-occlusion
capture_named high-140-shell-footprint "High 140km shell footprint" transition \
    "${high_transition_140[@]}" --cloud-debug-view orbit-shell-footprint
capture_named high-140-shell-filter "High 140km shell filter" transition \
    "${high_transition_140[@]}" --cloud-debug-view orbit-shell-filter
capture_named high-140-shell-mass "High 140km shell mass" transition \
    "${high_transition_140[@]}" --cloud-debug-view orbit-shell-mass
capture_named high-260-clouds "High 260km clouds" transition \
    "${high_transition_260[@]}"
capture_named high-260-no-clouds "High 260km no clouds" transition \
    "${high_transition_260[@]}" --no-clouds
capture_named high-260-distance "High 260km distance" transition \
    "${high_transition_260[@]}" --cloud-debug-view distance-regime
capture_named high-260-weights "High 260km weights" transition \
    "${high_transition_260[@]}" --cloud-debug-view transition-weights
capture_named high-260-shell-footprint "High 260km shell footprint" transition \
    "${high_transition_260[@]}" --cloud-debug-view orbit-shell-footprint
capture_named high-260-shell-filter "High 260km shell filter" transition \
    "${high_transition_260[@]}" --cloud-debug-view orbit-shell-filter
capture_named high-260-shell-mass "High 260km shell mass" transition \
    "${high_transition_260[@]}" --cloud-debug-view orbit-shell-mass

capture_named orbit-day-clouds "Orbit day clouds" orbit \
    "${orbit_day[@]}"
capture_named orbit-day-no-clouds "Orbit day no clouds" orbit \
    "${orbit_day[@]}" --no-clouds
capture_named orbit-terminator-clouds "Orbit terminator clouds" orbit \
    "${orbit_terminator[@]}"
capture_named orbit-coverage "Orbit coverage" orbit-diagnostics \
    "${orbit_day[@]}" --cloud-debug-view orbit-coverage
capture_named orbit-detail "Orbit detail" orbit-diagnostics \
    "${orbit_day[@]}" --cloud-debug-view orbit-detail
capture_named orbit-shell-alpha "Orbit shell alpha" orbit-diagnostics \
    "${orbit_day[@]}" --cloud-debug-view orbit-shell-alpha
capture_named orbit-shell-normal "Orbit shell normal" orbit-diagnostics \
    "${orbit_day[@]}" --cloud-debug-view orbit-shell-normal
capture_named orbit-shell-footprint "Orbit shell footprint" orbit-diagnostics \
    "${orbit_day[@]}" --cloud-debug-view orbit-shell-footprint
capture_named orbit-shell-filter "Orbit shell filter" orbit-diagnostics \
    "${orbit_day[@]}" --cloud-debug-view orbit-shell-filter
capture_named orbit-shell-mass "Orbit shell mass" orbit-diagnostics \
    "${orbit_day[@]}" --cloud-debug-view orbit-shell-mass

write_contact_sheet

printf 'planet cloud captures written to %s\n' "${OUT_DIR}"
