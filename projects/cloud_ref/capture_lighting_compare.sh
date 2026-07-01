#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/cloud_ref/cloud_ref}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-ref-lighting-compare-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-fair-weather}"
VIEW_STEPS="${VIEW_STEPS:-64}"
RESOLVE_MODE="${RESOLVE_MODE:-terrain-post}"
RESOLVE_RADIUS="${RESOLVE_RADIUS:-1.5}"
RESOLVE_STRENGTH="${RESOLVE_STRENGTH:-1.0}"
DEFAULT_AMBIENT="${DEFAULT_AMBIENT:-1.30}"
DEFAULT_DIRECT="${DEFAULT_DIRECT:-1.15}"
DEFAULT_PHASE="${DEFAULT_PHASE:-1.20}"
DEFAULT_POWDER="${DEFAULT_POWDER:-0.20}"
DEFAULT_SHADOW="${DEFAULT_SHADOW:-0.30}"

mkdir -p "${OUT_DIR}"
INDEX="${OUT_DIR}/index.md"
MANIFEST="${OUT_DIR}/manifest.tsv"
CAPTURES=()

write_header() {
    printf 'file\tview\tcase\tdebug\tambient\tdirect\tphase\tpowder\tshadow\n' >"${MANIFEST}"
    {
        printf '# Cloud Ref Lighting Compare\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Quality: %s\n' "${QUALITY}"
        printf -- '- Weather preset: %s\n' "${PRESET}"
        printf -- '- View steps: %s\n\n' "${VIEW_STEPS}"
        printf '| Capture | View | Case | Debug | Ambient | Direct | Phase | Powder | Shadow |\n'
        printf '|---|---|---|---|---|---|---|---|---|\n'
    } >"${INDEX}"
}

capture() {
    local name="$1"
    local view="$2"
    local case_name="$3"
    local debug="$4"
    local ambient="$5"
    local direct="$6"
    local phase="$7"
    local powder="$8"
    local shadow="$9"

    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${QUALITY}" \
        --cloud-weather-preset "${PRESET}" \
        --cloud-camera-mode "${view}" \
        --cloud-view-steps "${VIEW_STEPS}" \
        --cloud-view-samples 1 \
        --cloud-resolve-mode "${RESOLVE_MODE}" \
        --cloud-resolve-radius-px "${RESOLVE_RADIUS}" \
        --cloud-resolve-strength "${RESOLVE_STRENGTH}" \
        --cloud-ambient-strength "${ambient}" \
        --cloud-direct-strength "${direct}" \
        --cloud-phase-strength "${phase}" \
        --cloud-powder-strength "${powder}" \
        --cloud-shadow-strength "${shadow}" \
        --debug-view "${debug}" \
        --output "${OUT_DIR}/${name}.png"

    CAPTURES+=("${OUT_DIR}/${name}.png")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${view}" \
        "${case_name}" "${debug}" "${ambient}" "${direct}" "${phase}" "${powder}" \
        "${shadow}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | %s | %s | %s | %s | %s | %s |\n' "${case_name}" \
        "${name}.png" "${view}" "${case_name}" "${debug}" "${ambient}" "${direct}" \
        "${phase}" "${powder}" "${shadow}" >>"${INDEX}"
}

write_header

for view in surface-up surface-sun high-oblique; do
    capture "${view}-final" "${view}" "default final" final "${DEFAULT_AMBIENT}" \
        "${DEFAULT_DIRECT}" "${DEFAULT_PHASE}" "${DEFAULT_POWDER}" "${DEFAULT_SHADOW}"
    capture "${view}-ambient-only" "${view}" "ambient only" final "${DEFAULT_AMBIENT}" \
        0.00 0.00 "${DEFAULT_POWDER}" "${DEFAULT_SHADOW}"
    capture "${view}-direct-only" "${view}" "direct only" final 0.00 "${DEFAULT_DIRECT}" \
        0.00 "${DEFAULT_POWDER}" "${DEFAULT_SHADOW}"
    capture "${view}-phase-only" "${view}" "phase only" final 0.00 0.00 \
        "${DEFAULT_PHASE}" "${DEFAULT_POWDER}" "${DEFAULT_SHADOW}"
    capture "${view}-powder-035" "${view}" "powder 0.35" final "${DEFAULT_AMBIENT}" \
        "${DEFAULT_DIRECT}" "${DEFAULT_PHASE}" 0.35 "${DEFAULT_SHADOW}"
    capture "${view}-powder-070" "${view}" "powder 0.70" final "${DEFAULT_AMBIENT}" \
        "${DEFAULT_DIRECT}" "${DEFAULT_PHASE}" 0.70 "${DEFAULT_SHADOW}"
    capture "${view}-shadow-000" "${view}" "shadow 0.00" final "${DEFAULT_AMBIENT}" \
        "${DEFAULT_DIRECT}" "${DEFAULT_PHASE}" "${DEFAULT_POWDER}" 0.00
    capture "${view}-shadow-100" "${view}" "shadow 1.00" final "${DEFAULT_AMBIENT}" \
        "${DEFAULT_DIRECT}" "${DEFAULT_PHASE}" "${DEFAULT_POWDER}" 1.00
    capture "${view}-shadow-200" "${view}" "shadow 2.00" final "${DEFAULT_AMBIENT}" \
        "${DEFAULT_DIRECT}" "${DEFAULT_PHASE}" "${DEFAULT_POWDER}" 2.00
done

for debug in ambient-light direct-light phase-light lighting shadow cloud-alpha raw-final; do
    capture "surface-up-${debug}" surface-up "debug ${debug}" "${debug}" \
        "${DEFAULT_AMBIENT}" "${DEFAULT_DIRECT}" "${DEFAULT_PHASE}" "${DEFAULT_POWDER}" \
        "${DEFAULT_SHADOW}"
done

if command -v magick >/dev/null 2>&1; then
    rm -f "${OUT_DIR}/contact-sheet.png"
    magick montage "${CAPTURES[@]}" -geometry 320x180+8+8 -tile 4x \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'cloud_ref lighting compare captures written to %s\n' "${OUT_DIR}"
