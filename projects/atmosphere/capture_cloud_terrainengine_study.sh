#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/cloud-terrainengine-study-$(date +%Y%m%d-%H%M%S)}"
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
FILES=()
LABELS=()

write_header() {
    printf 'file\tcase\targs\n' >"${MANIFEST}"
    {
        printf '# TerrainEngine-Inspired Cloud Lighting Study\n\n'
        printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
        printf -- '- Frames: %s\n' "${FRAMES}"
        printf -- '- Reference: TerrainEngine-OpenGL `resources/pic2.jpg`\n\n'
        printf '| Capture | Case | Args |\n'
        printf '|---|---|---|\n'
    } >"${INDEX}"
}

capture_case() {
    local name="$1"
    local label="$2"
    shift 2
    local rel_file="${name}.png"
    local args=(
        --headless
        --frames "${FRAMES}"
        --width "${WIDTH}"
        --height "${HEIGHT}"
        --time-of-day-mode solar
        --time-hours 17.75
        --cloud-weather-preset surface-volume
        --cloud-quality full
        --cloud-view-steps 64
        --cloud-resolve-mode terrain-post
        --cloud-resolve-strength 1.0
        --cloud-debug-view final
        --camera-altitude-km 0.15
        --camera-pitch-offset-deg 18
        --no-reference-geometry
        --atmosphere-ground-mode sky-only-no-ground-occlusion
        "$@"
        --output "${OUT_DIR}/${rel_file}"
    )

    "${APP}" "${args[@]}"
    printf '%s\t%s\t%s\n' "${rel_file}" "${label}" "${args[*]}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | `%s` |\n' "${label}" "${rel_file}" "${label}" \
        "${args[*]}" >>"${INDEX}"
    FILES+=("${OUT_DIR}/${rel_file}")
    LABELS+=("${label}")
}

write_contact_sheet() {
    if ! command -v magick >/dev/null 2>&1; then
        return
    fi

    local montage_inputs=()
    local index
    for index in "${!FILES[@]}"; do
        montage_inputs+=("-label" "${LABELS[${index}]}" "${FILES[${index}]}")
    done
    magick montage "${montage_inputs[@]}" -geometry 420x236+8+24 -tile 2x \
        "${OUT_DIR}/contact-sheet.png"
}

write_header

capture_case default "Default low sun"
capture_case terrainengine-inspired "TerrainEngine-inspired" \
    --cloud-weather-preset broken-cumulus \
    --cloud-coverage 0.46 \
    --cloud-density 0.020 \
    --cloud-weather-scale-km 210 \
    --cloud-top-altitude-m 16000 \
    --cloud-shadow-strength 0.28 \
    --cloud-ambient-strength 1.02 \
    --cloud-direct-strength 1.36 \
    --cloud-phase-strength 1.62 \
    --cloud-twilight-color-strength 1.18 \
    --cloud-twilight-edge-strength 1.20 \
    --cloud-twilight-saturation-strength 1.22 \
    --cloud-afterglow-strength 0.56 \
    --cloud-powder-strength 0.42 \
    --cloud-final-contrast 1.10 \
    --cloud-final-saturation 1.10 \
    --cloud-sun-glare-strength 1.35
capture_case terrainengine-inspired-raw "TerrainEngine-inspired raw" \
    --cloud-weather-preset broken-cumulus \
    --cloud-coverage 0.46 \
    --cloud-density 0.020 \
    --cloud-weather-scale-km 210 \
    --cloud-top-altitude-m 16000 \
    --cloud-shadow-strength 0.28 \
    --cloud-ambient-strength 1.02 \
    --cloud-direct-strength 1.36 \
    --cloud-phase-strength 1.62 \
    --cloud-twilight-color-strength 1.18 \
    --cloud-twilight-edge-strength 1.20 \
    --cloud-twilight-saturation-strength 1.22 \
    --cloud-afterglow-strength 0.56 \
    --cloud-powder-strength 0.42 \
    --cloud-final-contrast 1.10 \
    --cloud-final-saturation 1.10 \
    --cloud-sun-glare-strength 1.35 \
    --cloud-debug-view raw-final

write_contact_sheet

printf 'TerrainEngine-inspired cloud study captures written to %s\n' "${OUT_DIR}"
