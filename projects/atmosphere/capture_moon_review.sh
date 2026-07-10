#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-moon-review-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"

mkdir -p "${OUT_DIR}/debug" "${OUT_DIR}/final"

if [[ ! -x "${APP}" ]]; then
    printf 'missing atmosphere app: %s\n' "${APP}" >&2
    exit 1
fi

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
DEBUG_FILES=()
DEBUG_LABELS=()
FINAL_FILES=()
FINAL_LABELS=()

write_header() {
    printf 'file\tgroup\tcase\targs\n' >"${MANIFEST}"
    {
        printf '# Atmosphere Moon Review\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n\n' "${FRAMES}"
        printf 'This pack reviews the shared geometry moon path from the standalone atmosphere '
        printf 'project. The moon debug views are readable phase checks, moon-surface is a '
        printf 'single material/detail check, and final views only review environment lighting '
        printf 'with clouds disabled.\n\n'
        printf '| Capture | Group | Case | Args |\n'
        printf '|---|---|---|---|\n'
    } >"${INDEX}"
}

record_capture() {
    local rel_file="$1"
    local label="$2"
    local group="$3"
    local case_name="$4"
    local args="$5"

    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\n' "${rel_file}" "${group}" "${case_name}" "${args}" \
        >>"${MANIFEST}"
    printf '| [%s](%s) | %s | %s | `%s` |\n' "${label}" "${rel_file}" "${group}" \
        "${case_name}" "${args}" >>"${INDEX}"

    if [[ "${group}" == "debug" ]]; then
        DEBUG_FILES+=("${OUT_DIR}/${rel_file}")
        DEBUG_LABELS+=("${label}")
    else
        FINAL_FILES+=("${OUT_DIR}/${rel_file}")
        FINAL_LABELS+=("${label}")
    fi
}

capture_atmosphere() {
    local rel_file="$1"
    local label="$2"
    local group="$3"
    local case_name="$4"
    shift 4

    local args=(
        --headless
        --frames "${FRAMES}"
        --width "${WIDTH}"
        --height "${HEIGHT}"
        "$@"
        --output "${OUT_DIR}/${rel_file}"
    )

    "${APP}" "${args[@]}"
    record_capture "${rel_file}" "${label}" "${group}" "${case_name}" "${args[*]}"
}

write_labeled_sheet() {
    local output="$1"
    local tile="$2"
    local geometry="$3"
    local -n files_ref="$4"
    local -n labels_ref="$5"

    if ! command -v magick >/dev/null 2>&1 || ((${#files_ref[@]} == 0)); then
        return
    fi

    local montage_inputs=()
    local index
    for index in "${!files_ref[@]}"; do
        montage_inputs+=("-label" "${labels_ref[${index}]}" "${files_ref[${index}]}")
    done
    rm -f "${output}"
    magick montage "${montage_inputs[@]}" -geometry "${geometry}" -tile "${tile}" "${output}"
}

common_final_args=(
    --time-of-day-mode solar
    --day-of-year 80
    --latitude-degrees 30
    --camera-altitude-km 0.15
    --pause-time
    --no-reference-geometry
    --no-clouds
)

write_header

for phase_name in new quarter full waning; do
    case "${phase_name}" in
    new)
        phase_offset="0.0"
        ;;
    quarter)
        phase_offset="7.382647"
        ;;
    full)
        phase_offset="14.765294"
        ;;
    waning)
        phase_offset="22.147941"
        ;;
    *)
        exit 1
        ;;
    esac

    capture_atmosphere "debug/moon-${phase_name}.png" "Moon ${phase_name}" debug \
        "moon ${phase_name}" \
        --debug-view moon \
        --time-of-day-mode solar \
        --day-of-year 80 \
        --time-hours 0 \
        --pause-time \
        --moon-phase-offset-days "${phase_offset}" \
        --camera-pitch-offset-deg 0 \
        --no-clouds
done

capture_atmosphere "debug/moon-surface.png" "Moon surface" debug "moon surface" \
    --debug-view moon-surface \
    --pause-time \
    --camera-pitch-offset-deg 0 \
    --no-clouds

capture_atmosphere "final/day-lighting.png" "Day environment" final day \
    "${common_final_args[@]}" \
    --time-hours 14.0 \
    --camera-pitch-offset-deg 15 \
    --moon-phase-offset-days 14.765294

capture_atmosphere "final/twilight-lighting.png" "Twilight environment" final twilight \
    "${common_final_args[@]}" \
    --time-hours 17.8 \
    --camera-pitch-offset-deg 10 \
    --moon-phase-offset-days 14.765294

capture_atmosphere "final/night-lighting.png" "Night environment" final night \
    "${common_final_args[@]}" \
    --time-hours 1.0 \
    --camera-pitch-offset-deg 8 \
    --moon-phase-offset-days 14.765294

write_labeled_sheet "${OUT_DIR}/debug-contact-sheet.png" 3x 360x203+8+24 DEBUG_FILES DEBUG_LABELS
write_labeled_sheet "${OUT_DIR}/final-contact-sheet.png" 3x 360x203+8+24 FINAL_FILES FINAL_LABELS

printf 'atmosphere moon review captures written to %s\n' "${OUT_DIR}"
