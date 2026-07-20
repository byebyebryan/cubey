#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/build/dev/assets/terrain/default}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/placement-control-v1}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"

if [[ ! -x "${APP}" ]]; then
    printf 'terrain executable not found: %s\n' "${APP}" >&2
    printf 'Build it with: cmake --build --preset dev --target cubey_project_terrain\n' >&2
    exit 1
fi
if [[ ! -f "${HEIGHTFIELD}/heightfield.json" && ! -f "${HEIGHTFIELD}" ]]; then
    printf 'terrain heightfield not found: %s\n' "${HEIGHTFIELD}" >&2
    printf 'Generate it with: cmake --build --preset dev --target cubey_terrain_generate_default_asset\n' >&2
    exit 1
fi

mkdir -p "${OUT_DIR}/profiles"
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 ! -name profiles -exec rm -rf {} +
find "${OUT_DIR}/profiles" -mindepth 1 -maxdepth 1 -delete

MANIFEST="${OUT_DIR}/manifest.tsv"
METRICS="${OUT_DIR}/placement-metrics.tsv"
INDEX="${OUT_DIR}/index.md"
COMPOSITION_FILES=()
COMPOSITION_LABELS=()
CLEARANCE_FILES=()
CLEARANCE_LABELS=()
ALL_FILES=()

PLACEMENTS=(
    "selected:selected:0"
    "raw-center:raw-center:0"
    "raw-sample-0:raw-sample:0"
    "raw-sample-1:raw-sample:1"
    "raw-sample-2:raw-sample:2"
)

printf 'file\ttitle\tgroup\tplacement\tplacement_index\targs\n' >"${MANIFEST}"
printf 'placement\tmode\tindex\tfocus_x_m\tfocus_z_m\tdirectional_contract\tscore\tlocal_relief_m\tlocal_p95_slope\tbaked_clearance_m\n' >"${METRICS}"
{
    printf '# Terrain Placement Control V1\n\n'
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- "- Heightfield: \`%s\`\n" "${HEIGHTFIELD}"
    printf -- '- Source: unchanged continuous raster in every lane\n'
    printf -- '- Product: high-density cached mesh, stride 3, filtered detail\n\n'
    printf '%s' "Start with \`composition-contact-sheet.png\`: rows are placement modes and columns "
    printf 'are explicit headings 0, 90, 180, and 270 degrees at 100 m. Then inspect '
    printf '%s' "\`clearance-contact-sheet.png\`: rows are the same placements and columns are 100 m "
    printf 'and the 500 m baked reference with the foreground sphere visible. Raw lanes are '
    printf 'negative controls; failed directional composition is valid evidence.\n\n'
    printf '| Capture | Group | Placement | Index | Arguments |\n'
    printf '|---|---|---|---:|---|\n'
} >"${INDEX}"

capture() {
    local name="$1"
    local title="$2"
    local group="$3"
    local mode="$4"
    local placement_index="$5"
    shift 5

    "${APP}" \
        --headless \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --terrain-heightfield "${HEIGHTFIELD}" \
        --terrain-placement "${mode}" \
        --terrain-placement-index "${placement_index}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="$*"
    args="${args//$'\t'/ }"
    ALL_FILES+=("${OUT_DIR}/${name}.png")
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${mode}" \
        "${placement_index}" "${args}" >>"${MANIFEST}"
    printf "| [%s](%s) | %s | \`%s\` | %s | \`%s\` |\n" "${title}" "${name}.png" "${group}" \
        "${mode}" "${placement_index}" "${args}" >>"${INDEX}"
}

metric_value() {
    local metrics_file="$1"
    local name="$2"
    awk -F, -v metric_name="${name}" \
        '$2 == "terrain.placement" && $3 == metric_name { print $4; exit }' "${metrics_file}"
}

for placement in "${PLACEMENTS[@]}"; do
    IFS=: read -r id mode placement_index <<<"${placement}"
    profile_prefix="${OUT_DIR}/profiles/${id}"
    for heading in 0 90 180 270; do
        name="composition-${id}-heading-${heading}"
        title="${id}, ${heading} deg"
        profile_args=()
        if [[ "${heading}" == "0" ]]; then
            profile_args=(--profile-output "${profile_prefix}")
        fi
        capture "${name}" "${title}" composition "${mode}" "${placement_index}" \
            --terrain-camera-preset backdrop \
            --terrain-surface-detail filtered-detail \
            --terrain-foreground-height 100 \
            --terrain-backdrop-azimuth "${heading}" \
            "${profile_args[@]}"
        COMPOSITION_FILES+=("${OUT_DIR}/${name}.png")
        COMPOSITION_LABELS+=("${title}")
    done

    metrics_file="${profile_prefix}.metrics.csv"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${id}" "${mode}" "${placement_index}" \
        "$(metric_value "${metrics_file}" source_focus_x_m)" \
        "$(metric_value "${metrics_file}" source_focus_z_m)" \
        "$(metric_value "${metrics_file}" directional_contract)" \
        "$(metric_value "${metrics_file}" score)" \
        "$(metric_value "${metrics_file}" local_relief_m)" \
        "$(metric_value "${metrics_file}" local_p95_slope)" \
        "$(metric_value "${metrics_file}" baked_clearance_m)" >>"${METRICS}"

    for foreground_height in 100 500; do
        name="clearance-${id}-${foreground_height}m"
        title="${id}, ${foreground_height} m"
        capture "${name}" "${title}" clearance "${mode}" "${placement_index}" \
            --terrain-camera-preset backdrop-stage \
            --terrain-surface-detail filtered-detail \
            --terrain-foreground-height "${foreground_height}" \
            --terrain-backdrop-azimuth 0
        CLEARANCE_FILES+=("${OUT_DIR}/${name}.png")
        CLEARANCE_LABELS+=("${title}")
    done
done

MANIFEST_PATH="${HEIGHTFIELD}"
if [[ -d "${HEIGHTFIELD}" ]]; then
    MANIFEST_PATH="${HEIGHTFIELD}/heightfield.json"
fi
GIT_REVISION="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
jq -n \
    --arg schema "cubey.terrain.placement-control.v1" \
    --arg git_revision "${GIT_REVISION}" \
    --arg executable "${APP}" \
    --arg heightfield_manifest "${MANIFEST_PATH}" \
    --arg elevation_sha256 "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" \
    --argjson capture_count "${#ALL_FILES[@]}" \
    '{
        schema: $schema,
        git_revision: $git_revision,
        executable: $executable,
        heightfield_manifest: $heightfield_manifest,
        elevation_sha256: $elevation_sha256,
        resolution: {width: $width, height: $height},
        placements: ["selected", "raw-center", "raw-sample:0", "raw-sample:1", "raw-sample:2"],
        headings_degrees: [0, 90, 180, 270],
        foreground_heights_m: [100, 500],
        capture_count: $capture_count
    }' >"${OUT_DIR}/review-metadata.json"

if command -v magick >/dev/null 2>&1; then
    composition_inputs=()
    for index in "${!COMPOSITION_FILES[@]}"; do
        composition_inputs+=("-label" "${COMPOSITION_LABELS[${index}]}" \
            "${COMPOSITION_FILES[${index}]}")
    done
    magick montage "${composition_inputs[@]}" -geometry 320x180+8+26 -tile 4x5 \
        "${OUT_DIR}/composition-contact-sheet.png"

    clearance_inputs=()
    for index in "${!CLEARANCE_FILES[@]}"; do
        clearance_inputs+=("-label" "${CLEARANCE_LABELS[${index}]}" \
            "${CLEARANCE_FILES[${index}]}")
    done
    magick montage "${clearance_inputs[@]}" -geometry 480x270+8+26 -tile 2x5 \
        "${OUT_DIR}/clearance-contact-sheet.png"
fi

printf 'Terrain placement control review written to %s\n' "${OUT_DIR}"
