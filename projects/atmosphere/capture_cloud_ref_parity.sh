#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ATMOSPHERE_APP="${ATMOSPHERE_APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
CLOUD_REF_APP="${CLOUD_REF_APP:-${ROOT_DIR}/build/dev/projects/cloud_ref/cloud_ref}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-ref-parity-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
CLOUD_REF_PRESET="${CLOUD_REF_PRESET:-fair-weather}"
ATMOSPHERE_PRESET="${ATMOSPHERE_PRESET:-reference-parity}"
VIEW_STEPS="${VIEW_STEPS:-64}"

mkdir -p "${OUT_DIR}/cloud_ref" "${OUT_DIR}/atmosphere"

if [[ ! -x "${ATMOSPHERE_APP}" ]]; then
    printf 'missing atmosphere app: %s\n' "${ATMOSPHERE_APP}" >&2
    exit 1
fi
if [[ ! -x "${CLOUD_REF_APP}" ]]; then
    printf 'missing cloud_ref app: %s\n' "${CLOUD_REF_APP}" >&2
    exit 1
fi

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

write_header() {
    printf 'file\tproject\tview\tcase\tdebug\targs\n' >"${MANIFEST}"
    {
        printf '# Cloud Ref / Atmosphere Parity Captures\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Cloud ref preset: %s\n' "${CLOUD_REF_PRESET}"
        printf -- '- Atmosphere preset: %s\n' "${ATMOSPHERE_PRESET}"
        printf -- '- View steps: %s\n\n' "${VIEW_STEPS}"
        printf '| Capture | Project | View | Case | Debug | Args |\n'
        printf '|---|---|---|---|---|---|\n'
    } >"${INDEX}"
}

record_capture() {
    local rel_file="$1"
    local label="$2"
    local project="$3"
    local view="$4"
    local case_name="$5"
    local debug="$6"
    local args="$7"

    CAPTURE_FILES+=("${OUT_DIR}/${rel_file}")
    CAPTURE_LABELS+=("${label}")
    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "${rel_file}" "${project}" "${view}" \
        "${case_name}" "${debug}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | %s | %s | `%s` |\n' "${label}" "${rel_file}" \
        "${project}" "${view}" "${case_name}" "${debug}" "${args}" >>"${INDEX}"
}

capture_cloud_ref() {
    local name="$1"
    local view="$2"
    local debug="$3"
    local rel_file="cloud_ref/${name}.png"
    local args=(--cloud-camera-mode "${view}" --debug-view "${debug}")

    "${CLOUD_REF_APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --cloud-quality full \
        --cloud-weather-preset "${CLOUD_REF_PRESET}" \
        --cloud-view-steps "${VIEW_STEPS}" \
        --cloud-view-samples 1 \
        --cloud-resolve-mode terrain-post \
        --cloud-resolve-radius-px 1.5 \
        --cloud-resolve-strength 1.0 \
        "${args[@]}" \
        --output "${OUT_DIR}/${rel_file}"

    record_capture "${rel_file}" "cloud_ref ${view} ${debug}" "cloud_ref" "${view}" \
        "reference" "${debug}" "${args[*]}"
}

capture_atmosphere() {
    local name="$1"
    local view="$2"
    local case_name="$3"
    local debug="$4"
    shift 4
    local rel_file="atmosphere/${name}.png"
    local local_args=(
        --time-of-day-mode solar
        --cloud-weather-preset "${ATMOSPHERE_PRESET}"
        --cloud-view-steps "${VIEW_STEPS}"
        --cloud-resolve-radius-px 1.5
        --cloud-debug-view "${debug}"
        "$@"
    )

    "${ATMOSPHERE_APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        "${local_args[@]}" \
        --output "${OUT_DIR}/${rel_file}"

    record_capture "${rel_file}" "atmo ${view} ${case_name} ${debug}" "atmosphere" \
        "${view}" "${case_name}" "${debug}" "${local_args[*]}"
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

write_header

for debug in final raw-final cloud-alpha lighting ambient-light direct-light phase-light shadow; do
    capture_cloud_ref "surface-up-${debug}" surface-up "${debug}"
done
capture_cloud_ref surface-sun-final surface-sun final
capture_cloud_ref surface-horizon-final surface final

capture_atmosphere surface-up-noon-final surface-up noon final \
    --time-hours 14.0 --camera-altitude-km 0.15 --camera-pitch-offset-deg 45
capture_atmosphere surface-up-noon-raw surface-up noon raw-final \
    --time-hours 14.0 --camera-altitude-km 0.15 --camera-pitch-offset-deg 45
capture_atmosphere surface-up-noon-lighting surface-up noon lighting \
    --time-hours 14.0 --camera-altitude-km 0.15 --camera-pitch-offset-deg 45
capture_atmosphere surface-up-noon-shadow surface-up noon shadow \
    --time-hours 14.0 --camera-altitude-km 0.15 --camera-pitch-offset-deg 45

capture_atmosphere surface-horizon-noon-final surface-horizon noon final \
    --time-hours 14.0 --camera-altitude-km 0.15 --camera-pitch-offset-deg 0
capture_atmosphere surface-horizon-twilight-final surface-horizon twilight final \
    --time-hours 17.8 --camera-altitude-km 0.15 --camera-pitch-offset-deg 0
capture_atmosphere surface-up-twilight-final surface-up twilight final \
    --time-hours 17.8 --camera-altitude-km 0.15 --camera-pitch-offset-deg 45
capture_atmosphere surface-horizon-night-final surface-horizon night final \
    --time-hours 1.0 --camera-altitude-km 0.15 --camera-pitch-offset-deg 0
capture_atmosphere surface-up-night-final surface-up night final \
    --time-hours 1.0 --camera-altitude-km 0.15 --camera-pitch-offset-deg 45

write_contact_sheet

printf 'cloud parity captures written to %s\n' "${OUT_DIR}"
