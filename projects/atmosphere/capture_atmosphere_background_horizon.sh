#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-background-horizon-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"

mkdir -p "${OUT_DIR}"

if [[ ! -x "${APP}" ]]; then
    printf 'missing atmosphere app: %s\n' "${APP}" >&2
    exit 1
fi

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

write_header() {
    printf 'file\tground_mode\tcase\targs\n' >"${MANIFEST}"
    {
        printf '# Atmosphere Background Horizon Captures\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n\n' "${FRAMES}"
        printf 'This pack disables clouds and reference geometry so horizon bands can be '
        printf 'assigned to the clear-sky/background path instead of the cloud layer.\n\n'
        printf '| Capture | Ground mode | Case | Args |\n'
        printf '|---|---|---|---|\n'
    } >"${INDEX}"
}

record_capture() {
    local rel_file="$1"
    local label="$2"
    local ground_mode="$3"
    local case_name="$4"
    local args="$5"

    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\n' "${rel_file}" "${ground_mode}" "${case_name}" \
        "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | `%s` |\n' "${label}" "${rel_file}" \
        "${ground_mode}" "${case_name}" "${args}" >>"${INDEX}"
    CAPTURE_FILES+=("${OUT_DIR}/${rel_file}")
    CAPTURE_LABELS+=("${label}")
}

capture_case() {
    local ground_mode="$1"
    local case_name="$2"
    local hour="$3"
    local rel_file="${ground_mode}-${case_name}.png"
    local args=(
        --time-of-day-mode solar
        --time-hours "${hour}"
        --camera-altitude-km 0.15
        --camera-pitch-offset-deg 0
        --no-reference-geometry
        --no-clouds
        --atmosphere-ground-mode "${ground_mode}"
    )

    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        "${args[@]}" \
        --output "${OUT_DIR}/${rel_file}"

    record_capture "${rel_file}" "${ground_mode} ${case_name}" "${ground_mode}" \
        "${case_name}" "${args[*]}"
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

write_header

for case_name in noon twilight night; do
    case "${case_name}" in
    noon)
        hour="14.0"
        ;;
    twilight)
        hour="17.8"
        ;;
    night)
        hour="1.0"
        ;;
    *)
        exit 1
        ;;
    esac

    for ground_mode in ground sky-only sky-only-no-ground-occlusion; do
        capture_case "${ground_mode}" "${case_name}" "${hour}"
    done
done

write_contact_sheet

printf 'atmosphere background horizon captures written to %s\n' "${OUT_DIR}"
