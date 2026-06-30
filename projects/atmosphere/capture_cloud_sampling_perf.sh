#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ATMO_APP="${ATMO_APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
CLOUD_REF_APP="${CLOUD_REF_APP:-${ROOT_DIR}/build/dev/projects/cloud_ref/cloud_ref}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-cloud-sampling-perf-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-90}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
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
    printf 'file\ttitle\tapp\tview\tquality\tview_steps\tview_samples\targs\n' >"${MANIFEST}"
    {
        printf '# Atmosphere Cloud Sampling Performance Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Warmup frames: %s\n' "${WARMUP_FRAMES}"
        printf -- '- Weather preset: %s\n\n' "${PRESET}"
        printf '| Capture | App | View | Quality | Steps | Samples | Args |\n'
        printf '|---|---|---|---|---|---|---|\n'
    } >"${INDEX}"
}

capture_atmosphere() {
    local name="$1"
    local title="$2"
    local view="$3"
    local quality="$4"
    local samples="$5"
    shift 5

    "${ATMO_APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${quality}" \
        --cloud-weather-preset "${PRESET}" \
        --cloud-view-samples "${samples}" \
        --profile-output "${OUT_DIR}/profiles/${name}" \
        --profile-warmup-frames "${WARMUP_FRAMES}" \
        --profile-diagnostics \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="--cloud-quality ${quality} --cloud-view-samples ${samples} $*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\tatmosphere\t%s\t%s\tpreset\t%s\t%s\n' "${name}.png" "${title}" \
        "${view}" "${quality}" "${samples}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | atmosphere | %s | %s | preset | %s | `%s` |\n' "${title}" \
        "${name}.png" "${view}" "${quality}" "${samples}" "${args}" >>"${INDEX}"
}

capture_cloud_ref() {
    local name="$1"
    local title="$2"
    local view="$3"
    local quality="$4"
    local steps="$5"
    local samples="$6"
    shift 6

    "${CLOUD_REF_APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality "${quality}" \
        --cloud-weather-preset "${PRESET}" \
        --cloud-view-steps "${steps}" \
        --cloud-view-samples "${samples}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="--cloud-quality ${quality} --cloud-view-steps ${steps} --cloud-view-samples ${samples} $*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\tcloud_ref\t%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${view}" \
        "${quality}" "${steps}" "${samples}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | cloud_ref | %s | %s | %s | %s | `%s` |\n' "${title}" \
        "${name}.png" "${view}" "${quality}" "${steps}" "${samples}" "${args}" >>"${INDEX}"
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

for quality in quarter half full; do
    capture_atmosphere "atmo-surface-up-${quality}-s1" "Atmo surface-up ${quality} s1" \
        surface-up "${quality}" 1 "${surface_up_day[@]}"
    capture_atmosphere "atmo-surface-up-${quality}-s2" "Atmo surface-up ${quality} s2" \
        surface-up "${quality}" 2 "${surface_up_day[@]}"
    capture_atmosphere "atmo-high-oblique-${quality}-s1" "Atmo high-oblique ${quality} s1" \
        high-oblique "${quality}" 1 "${high_oblique_day[@]}"
    capture_atmosphere "atmo-high-oblique-${quality}-s2" "Atmo high-oblique ${quality} s2" \
        high-oblique "${quality}" 2 "${high_oblique_day[@]}"
done

capture_cloud_ref "cloud-ref-surface-up-s1" "Cloud ref surface-up steps64 s1" surface-up full \
    64 1 --cloud-camera-mode surface-up
capture_cloud_ref "cloud-ref-surface-up-s2" "Cloud ref surface-up steps64 s2" surface-up full \
    64 2 --cloud-camera-mode surface-up
capture_cloud_ref "cloud-ref-high-oblique-s1" "Cloud ref high-oblique steps64 s1" high-oblique \
    full 64 1 --cloud-camera-mode high-oblique
capture_cloud_ref "cloud-ref-high-oblique-s2" "Cloud ref high-oblique steps64 s2" high-oblique \
    full 64 2 --cloud-camera-mode high-oblique

write_contact_sheet

printf 'atmosphere cloud sampling/perf captures written to %s\n' "${OUT_DIR}"
