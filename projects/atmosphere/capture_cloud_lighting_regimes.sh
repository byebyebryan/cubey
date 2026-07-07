#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-lighting-regimes-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
PRESET="${PRESET:-surface-volume}"
VIEW_STEPS="${VIEW_STEPS:-64}"
AFTERGLOW_STRENGTH="${AFTERGLOW_STRENGTH:-0.75}"
AFTERGLOW_HOUR="${AFTERGLOW_HOUR:-18.1}"

mkdir -p "${OUT_DIR}/surface-up" "${OUT_DIR}/horizon"

if [[ ! -x "${APP}" ]]; then
    printf 'missing atmosphere app: %s\n' "${APP}" >&2
    exit 1
fi

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
SURFACE_FILES=()
SURFACE_LABELS=()
HORIZON_FILES=()
HORIZON_LABELS=()

write_header() {
    printf 'file\tview\tcase\tdebug\targs\n' >"${MANIFEST}"
    {
        printf '# Cloud Lighting Regime Captures\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Weather preset: %s\n' "${PRESET}"
        printf -- '- View steps: %s\n' "${VIEW_STEPS}"
        printf -- '- Afterglow showcase strength: %s\n' "${AFTERGLOW_STRENGTH}"
        printf -- '- Afterglow showcase hour: %s\n\n' "${AFTERGLOW_HOUR}"
        printf '| Capture | View | Case | Debug | Args |\n'
        printf '|---|---|---|---|---|\n'
    } >"${INDEX}"
}

record_capture() {
    local rel_file="$1"
    local label="$2"
    local view="$3"
    local case_name="$4"
    local debug="$5"
    local args="$6"

    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\t%s\n' "${rel_file}" "${view}" "${case_name}" \
        "${debug}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | %s | `%s` |\n' "${label}" "${rel_file}" \
        "${view}" "${case_name}" "${debug}" "${args}" >>"${INDEX}"

    if [[ "${view}" == "surface-up" ]]; then
        SURFACE_FILES+=("${OUT_DIR}/${rel_file}")
        SURFACE_LABELS+=("${label}")
    else
        HORIZON_FILES+=("${OUT_DIR}/${rel_file}")
        HORIZON_LABELS+=("${label}")
    fi
}

capture_atmosphere() {
    local name="$1"
    local view="$2"
    local case_name="$3"
    local debug="$4"
    local pitch="$5"
    local hour="$6"
    local rel_file="${view}/${name}.png"
    local local_args=(
        --time-of-day-mode solar
        --time-hours "${hour}"
        --cloud-weather-preset "${PRESET}"
        --cloud-view-steps "${VIEW_STEPS}"
        --cloud-debug-view "${debug}"
        --camera-altitude-km 0.15
        --camera-pitch-offset-deg "${pitch}"
        --no-reference-geometry
        --atmosphere-ground-mode sky-only-no-ground-occlusion
    )
    if ((${#CASE_EXTRA_ARGS[@]} > 0)); then
        local_args+=("${CASE_EXTRA_ARGS[@]}")
    fi

    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        "${local_args[@]}" \
        --output "${OUT_DIR}/${rel_file}"

    record_capture "${rel_file}" "${view} ${case_name} ${debug}" "${view}" \
        "${case_name}" "${debug}" "${local_args[*]}"
}

write_contact_sheet() {
    local output="$1"
    local tile="$2"
    local geometry="$3"
    shift 3

    if ! command -v magick >/dev/null 2>&1; then
        return
    fi

    rm -f "${output}"
    magick montage "$@" -geometry "${geometry}" -tile "${tile}" "${output}"
}

write_labeled_sheet() {
    local output="$1"
    local tile="$2"
    local geometry="$3"
    local -n files_ref="$4"
    local -n labels_ref="$5"

    local montage_inputs=()
    local index
    for index in "${!files_ref[@]}"; do
        montage_inputs+=("-label" "${labels_ref[${index}]}" "${files_ref[${index}]}")
    done

    write_contact_sheet "${output}" "${tile}" "${geometry}" "${montage_inputs[@]}"
}

write_header

for case_name in noon twilight afterglow night; do
    CASE_EXTRA_ARGS=()
    case "${case_name}" in
    noon)
        hour="14.0"
        ;;
    twilight)
        hour="17.8"
        ;;
    afterglow)
        hour="${AFTERGLOW_HOUR}"
        CASE_EXTRA_ARGS=(--cloud-afterglow-strength "${AFTERGLOW_STRENGTH}")
        ;;
    night)
        hour="1.0"
        ;;
    *)
        exit 1
        ;;
    esac

    for debug in final raw-final lighting ambient-light cloud-alpha background; do
        capture_atmosphere "${case_name}-${debug}" surface-up "${case_name}" "${debug}" 45 "${hour}"
    done
    for debug in final raw-final lighting cloud-alpha background edge-mask distance; do
        capture_atmosphere "${case_name}-${debug}" horizon "${case_name}" "${debug}" 0 "${hour}"
    done
done

write_labeled_sheet "${OUT_DIR}/surface-up-contact-sheet.png" 6x \
    320x180+8+24 SURFACE_FILES SURFACE_LABELS
write_labeled_sheet "${OUT_DIR}/horizon-contact-sheet.png" 7x \
    274x154+6+20 HORIZON_FILES HORIZON_LABELS

printf 'cloud lighting regime captures written to %s\n' "${OUT_DIR}"
