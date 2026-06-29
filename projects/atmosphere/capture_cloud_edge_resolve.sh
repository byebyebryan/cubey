#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-cloud-edge-resolve-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-full}"
SECONDARY_QUALITY="${SECONDARY_QUALITY:-half}"
PRESET="${PRESET:-broken-cumulus}"

mkdir -p "${OUT_DIR}"

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

surface_up_day=(
    --time-of-day-mode solar
    --time-hours 14.0
    --camera-altitude-km 0.15
    --camera-pitch-offset-deg 45
    --cloud-distance-mode auto
)

high_oblique_day=(
    --time-of-day-mode solar
    --time-hours 14.0
    --camera-altitude-km 28.0
    --camera-pitch-offset-deg -25
    --cloud-distance-mode auto
)

write_index_header() {
    printf 'file\ttitle\tview\tresolve_mode\tquality\tdebug_view\targs\n' >"${MANIFEST}"
    {
        printf '# Atmosphere Cloud Edge Resolve Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Quality: %s\n' "${QUALITY}"
        printf -- '- Secondary quality: %s\n' "${SECONDARY_QUALITY}"
        printf -- '- Weather preset: %s\n\n' "${PRESET}"
        printf '| Capture | View | Resolve | Quality | Debug | Args |\n'
        printf '|---|---|---|---|---|---|\n'
    } >"${INDEX}"
}

capture_named() {
    local name="$1"
    local title="$2"
    local view="$3"
    local resolve_mode="$4"
    local quality="$5"
    local debug_view="$6"
    shift 6

    local debug_args=()
    if [[ "${debug_view}" != "final" ]]; then
        debug_args=(--cloud-debug-view "${debug_view}")
    fi

    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${quality}" \
        --cloud-weather-preset "${PRESET}" \
        --cloud-resolve-mode "${resolve_mode}" \
        "$@" \
        "${debug_args[@]}" \
        --output "${OUT_DIR}/${name}.png"

    local args="--cloud-quality ${quality} --cloud-resolve-mode ${resolve_mode} $* ${debug_args[*]}"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${view}" \
        "${resolve_mode}" "${quality}" "${debug_view}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | %s | %s | `%s` |\n' "${title}" "${name}.png" \
        "${view}" "${resolve_mode}" "${quality}" "${debug_view}" "${args}" >>"${INDEX}"
}

capture_matrix() {
    local view="$1"
    shift
    local -a view_args=("$@")
    local resolve_mode
    local debug_view
    for resolve_mode in terrain-post metadata-bilateral; do
        for debug_view in final raw-final cloud-alpha edge-mask; do
            capture_named "${view}-${resolve_mode}-${debug_view}" \
                "${view} ${resolve_mode} ${debug_view}" "${view}" "${resolve_mode}" \
                "${QUALITY}" "${debug_view}" "${view_args[@]}"
        done
        capture_named "${view}-${resolve_mode}-${SECONDARY_QUALITY}-final" \
            "${view} ${resolve_mode} ${SECONDARY_QUALITY} final" "${view}" \
            "${resolve_mode}" "${SECONDARY_QUALITY}" final "${view_args[@]}"
    done
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

write_index_header
capture_matrix surface-up "${surface_up_day[@]}"
capture_matrix high-oblique "${high_oblique_day[@]}"
write_contact_sheet

printf 'atmosphere cloud edge resolve captures written to %s\n' "${OUT_DIR}"
