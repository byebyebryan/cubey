#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-surface-horizon-regimes-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
QUALITY="${QUALITY:-full}"
PRESET="${PRESET:-surface-volume}"
VIEW_STEPS="${VIEW_STEPS:-64}"
AFTERGLOW_STRENGTH="${AFTERGLOW_STRENGTH:-0.75}"
AFTERGLOW_HOUR="${AFTERGLOW_HOUR:-18.1}"

mkdir -p "${OUT_DIR}/local-only" "${OUT_DIR}/auto-handoff" "${OUT_DIR}/no-clouds"

if [[ ! -x "${APP}" ]]; then
    printf 'missing atmosphere app: %s\n' "${APP}" >&2
    exit 1
fi

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

write_header() {
    printf 'file\tvariant\tcase\tdebug\targs\n' >"${MANIFEST}"
    {
        printf '# Cloud Surface Horizon Regime Captures\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Quality: %s\n' "${QUALITY}"
        printf -- '- Weather preset: %s\n' "${PRESET}"
        printf -- '- View steps: %s\n' "${VIEW_STEPS}"
        printf -- '- Afterglow showcase strength: %s\n' "${AFTERGLOW_STRENGTH}"
        printf -- '- Afterglow showcase hour: %s\n\n' "${AFTERGLOW_HOUR}"
        printf 'Cloud V1 acceptance is the surface view with the production lower-sky '
        printf 'horizon handoff. Local-only captures remain the reference fallback.\n\n'
        printf 'Variants:\n\n'
        printf -- '- `local-only`: surface-volume with local distance mode and no horizon layer.\n'
        printf -- '- `auto-handoff`: production default with auto distance mode and horizon layer enabled.\n'
        printf -- '- `no-clouds`: clear-sky/background comparison for horizon bands.\n\n'
        printf '| Capture | Variant | Case | Debug | Args |\n'
        printf '|---|---|---|---|---|\n'
    } >"${INDEX}"
}

record_capture() {
    local rel_file="$1"
    local label="$2"
    local variant="$3"
    local case_name="$4"
    local debug="$5"
    local args="$6"

    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\t%s\n' "${rel_file}" "${variant}" "${case_name}" \
        "${debug}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | %s | `%s` |\n' "${label}" "${rel_file}" \
        "${variant}" "${case_name}" "${debug}" "${args}" >>"${INDEX}"
    CAPTURE_FILES+=("${OUT_DIR}/${rel_file}")
    CAPTURE_LABELS+=("${label}")
}

capture_atmosphere() {
    local variant="$1"
    local case_name="$2"
    local debug="$3"
    local hour="$4"
    shift 4
    local rel_file="${variant}/${case_name}-${debug}.png"
    local local_args=(
        --time-of-day-mode solar
        --time-hours "${hour}"
        --cloud-weather-preset "${PRESET}"
        --cloud-quality "${QUALITY}"
        --cloud-view-steps "${VIEW_STEPS}"
        --cloud-debug-view "${debug}"
        --camera-altitude-km 0.15
        --camera-pitch-offset-deg 0
        --no-reference-geometry
        --atmosphere-ground-mode sky-only-no-ground-occlusion
        "$@"
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

    record_capture "${rel_file}" "${variant} ${case_name} ${debug}" "${variant}" \
        "${case_name}" "${debug}" "${local_args[*]}"
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
    magick montage "${montage_inputs[@]}" -geometry 274x154+6+22 -tile 8x \
        "${OUT_DIR}/contact-sheet.png"
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

    for debug in final raw-final background cloud-alpha horizon-handoff \
        local-truncation integrated-horizon-alpha integrated-horizon-radiance; do
        capture_atmosphere local-only "${case_name}" "${debug}" "${hour}" \
            --cloud-distance-mode local --no-cloud-horizon-layer
        capture_atmosphere auto-handoff "${case_name}" "${debug}" "${hour}" \
            --cloud-distance-mode auto --cloud-horizon-layer
    done

    for debug in final background; do
        capture_atmosphere no-clouds "${case_name}" "${debug}" "${hour}" --no-clouds
    done
done

write_contact_sheet

printf 'cloud surface horizon regime captures written to %s\n' "${OUT_DIR}"
