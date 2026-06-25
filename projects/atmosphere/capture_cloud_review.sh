#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-cloud-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-broken-cumulus}"
DEEP="${DEEP:-0}"
MOTION_FRAMES="${MOTION_FRAMES:-120}"
MOTION_FPS="${MOTION_FPS:-30}"

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
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"
}

write_index_header() {
    printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
    {
        printf '# Atmosphere Cloud Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Quality: %s\n' "${QUALITY}"
        printf -- '- Weather preset: %s\n' "${PRESET}"
        printf -- '- Deep diagnostics: %s\n\n' "${DEEP}"
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
    magick montage "${montage_inputs[@]}" -geometry 320x180+8+26 -tile 2x \
        "${OUT_DIR}/contact-sheet.png"
}

surface_horizon_day=(
    --time-of-day-mode solar
    --time-hours 14.0
    --camera-altitude-km 0.15
    --camera-pitch-offset-deg 0
)
surface_up_day=(
    --time-of-day-mode solar
    --time-hours 14.0
    --camera-altitude-km 0.15
    --camera-pitch-offset-deg 45
)
surface_horizon_sunset=(
    --time-of-day-mode solar
    --time-hours 17.8
    --camera-altitude-km 0.15
    --camera-pitch-offset-deg 0
)
high_oblique_day=(
    --time-of-day-mode solar
    --time-hours 14.0
    --camera-altitude-km 28.0
    --camera-pitch-offset-deg -25
    --cloud-distance-mode auto
)
high_up_day=(
    --time-of-day-mode solar
    --time-hours 14.0
    --camera-altitude-km 28.0
    --camera-pitch-offset-deg 30
    --cloud-distance-mode auto
)
orbit_shell_oblique_day=(
    --time-of-day-mode solar
    --time-hours 14.0
    --camera-altitude-km 80.0
    --camera-pitch-offset-deg -45
    --cloud-distance-mode orbit-shell
    --cloud-orbit-representation surface-shell
)

write_index_header

capture_named surface-horizon-final "Surface horizon final" final \
    "${surface_horizon_day[@]}"
capture_named surface-horizon-no-clouds "Surface horizon no clouds" final \
    "${surface_horizon_day[@]}" --no-clouds
capture_named surface-up-final "Surface upward final" final \
    "${surface_up_day[@]}"
capture_named sunset-horizon-final "Sunset horizon final" final \
    "${surface_horizon_sunset[@]}"

capture_named surface-up-raw-final "Surface upward raw final" diagnostics \
    "${surface_up_day[@]}" --cloud-debug-view raw-final
capture_named surface-up-cloud-alpha "Surface upward cloud alpha" diagnostics \
    "${surface_up_day[@]}" --cloud-debug-view cloud-alpha
capture_named surface-up-density "Surface upward density" diagnostics \
    "${surface_up_day[@]}" --cloud-debug-view density
capture_named surface-up-lighting "Surface upward lighting" diagnostics \
    "${surface_up_day[@]}" --cloud-debug-view lighting
capture_named surface-up-local-structure "Surface upward local structure" diagnostics \
    "${surface_up_day[@]}" --cloud-debug-view local-structure
capture_named surface-up-local-edge-detail "Surface upward local edge detail" diagnostics \
    "${surface_up_day[@]}" --cloud-debug-view local-edge-detail
capture_named surface-up-coverage-bias "Surface upward coverage bias" diagnostics \
    "${surface_up_day[@]}" --cloud-debug-view coverage-bias

capture_named high-oblique-final "High oblique final" transition \
    "${high_oblique_day[@]}"
capture_named high-oblique-distance-regime "High oblique distance regime" transition \
    "${high_oblique_day[@]}" --cloud-debug-view distance-regime
capture_named high-oblique-transition-weights "High oblique transition weights" transition \
    "${high_oblique_day[@]}" --cloud-debug-view transition-weights
capture_named high-up-final "High upward final" transition \
    "${high_up_day[@]}"

capture_named orbit-shell-oblique-final "Orbit shell oblique final" orbit \
    "${orbit_shell_oblique_day[@]}"
capture_named orbit-shell-coverage "Orbit shell coverage" orbit \
    "${orbit_shell_oblique_day[@]}" --cloud-debug-view orbit-coverage
capture_named orbit-shell-normal "Orbit shell normal" orbit \
    "${orbit_shell_oblique_day[@]}" --cloud-debug-view orbit-shell-normal
capture_named orbit-shell-alpha "Orbit shell alpha" orbit \
    "${orbit_shell_oblique_day[@]}" --cloud-debug-view orbit-shell-alpha

if [[ "${DEEP}" != "0" ]]; then
    motion_video="${OUT_DIR}/surface-up-motion.mp4"
    "${APP}" \
        --headless \
        --capture video \
        --frames "${MOTION_FRAMES}" \
        --fps "${MOTION_FPS}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        "${surface_up_day[@]}" \
        --time-speed-hours-per-second 0.5 \
        --cloud-wind-speed-mps 900 \
        --output "${motion_video}"

    capture_named surface-up-transmittance "Surface upward transmittance" deep-diagnostics \
        "${surface_up_day[@]}" --cloud-debug-view transmittance
    capture_named surface-up-ambient-light "Surface upward ambient light" deep-diagnostics \
        "${surface_up_day[@]}" --cloud-debug-view ambient-light
    capture_named surface-up-direct-light "Surface upward direct light" deep-diagnostics \
        "${surface_up_day[@]}" --cloud-debug-view direct-light
    capture_named surface-up-phase-light "Surface upward phase light" deep-diagnostics \
        "${surface_up_day[@]}" --cloud-debug-view phase-light
    capture_named surface-up-metadata-confidence "Surface upward metadata confidence" deep-diagnostics \
        "${surface_up_day[@]}" --cloud-debug-view metadata-confidence
    capture_named surface-up-steps "Surface upward steps" deep-diagnostics \
        "${surface_up_day[@]}" --cloud-debug-view steps
fi

write_contact_sheet

printf 'atmosphere cloud captures written to %s\n' "${OUT_DIR}"
