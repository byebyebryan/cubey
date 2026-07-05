#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/cloud_ref/cloud_ref}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-ref-sampling-compare-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-fair-weather}"
VIEW_STEPS="${VIEW_STEPS:-64}"
RESOLVE_RADIUS="${RESOLVE_RADIUS:-1.5}"
RESOLVE_STRENGTH="${RESOLVE_STRENGTH:-1.0}"

mkdir -p "${OUT_DIR}"
INDEX="${OUT_DIR}/index.md"
MANIFEST="${OUT_DIR}/manifest.tsv"
CAPTURES=()

write_header() {
    printf 'file\tview\tcase\tview_steps\tview_samples\tresolve_mode\tresolve_radius\tresolve_strength\n' \
        >"${MANIFEST}"
    {
        printf '# Cloud Ref Sampling Compare\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Quality: %s\n' "${QUALITY}"
        printf -- '- Weather preset: %s\n' "${PRESET}"
        printf -- '- View steps: %s\n\n' "${VIEW_STEPS}"
        printf '| Capture | View | Case | Samples | Resolve | Radius | Strength |\n'
        printf '|---|---|---|---|---|---|---|\n'
    } >"${INDEX}"
}

capture() {
    local name="$1"
    local view="$2"
    local title="$3"
    local samples="$4"
    local resolve_mode="$5"
    shift 5

    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        --cloud-camera-mode "${view}" \
        --cloud-view-steps "${VIEW_STEPS}" \
        --cloud-view-samples "${samples}" \
        --cloud-resolve-mode "${resolve_mode}" \
        --cloud-resolve-radius-px "${RESOLVE_RADIUS}" \
        --cloud-resolve-strength "${RESOLVE_STRENGTH}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    CAPTURES+=("${OUT_DIR}/${name}.png")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${view}" "${title}" \
        "${VIEW_STEPS}" "${samples}" "${resolve_mode}" "${RESOLVE_RADIUS}" \
        "${RESOLVE_STRENGTH}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | %s | %s | %s | %s |\n' "${title}" "${name}.png" \
        "${view}" "${title}" "${samples}" "${resolve_mode}" "${RESOLVE_RADIUS}" \
        "${RESOLVE_STRENGTH}" >>"${INDEX}"
}

write_header

for view in surface-up high-oblique; do
    capture "${view}-s1-terrain" "${view}" "${view} s1 terrain-post" 1 terrain-post
    capture "${view}-s1-bilateral" "${view}" "${view} s1 metadata-bilateral" 1 \
        metadata-bilateral
    capture "${view}-s2-terrain" "${view}" "${view} s2 terrain-post" 2 terrain-post
done

if command -v magick >/dev/null 2>&1; then
    rm -f "${OUT_DIR}/contact-sheet.png"
    magick montage "${CAPTURES[@]}" -geometry 480x270+8+8 -tile 3x \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'cloud_ref sampling compare captures written to %s\n' "${OUT_DIR}"
