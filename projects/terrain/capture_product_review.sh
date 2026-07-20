#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/build/dev/assets/terrain/default}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/product-v1}"
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

mkdir -p "${OUT_DIR}"
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 -delete

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
{
    printf '# Terrain Product V1 Review\n\n'
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Heightfield: `%s`\n' "${HEIGHTFIELD}"
    printf -- '- Product: continuous seam-matched center, high density, stride 3\n'
    printf -- '- Camera: 100 m foreground focus, unrestricted yaw, 50-250 m orbit\n\n'
    printf 'The final views judge far-field composition. Diagnostics verify that the '
    printf 'same source geometry drives height, slope, classification, topology, and material.\n\n'
    printf '| Capture | Group | Arguments |\n'
    printf '|---|---|---|\n'
} >"${INDEX}"

capture() {
    local name="$1"
    local title="$2"
    local group="$3"
    shift 3

    "${APP}" \
        --headless \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --terrain-heightfield "${HEIGHTFIELD}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="$*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | `%s` |\n' "${title}" "${name}.png" "${group}" "${args}" >>"${INDEX}"
}

capture final-stage "Filtered detail with foreground" final \
    --terrain-camera-preset backdrop-stage \
    --terrain-surface-detail filtered-detail
capture final-clean "Filtered detail clean view" final \
    --terrain-camera-preset backdrop \
    --terrain-surface-detail filtered-detail
capture final-flat "Flat material control" material \
    --terrain-camera-preset backdrop \
    --terrain-surface-detail flat
capture final-raking-light "Filtered detail, raking light" lighting \
    --terrain-camera-preset backdrop \
    --terrain-surface-detail filtered-detail \
    --sun-elevation 12 \
    --sun-azimuth 35

for heading in 0 90 180 270; do
    capture "heading-${heading}" "Heading ${heading} deg" heading \
        --terrain-camera-preset backdrop \
        --terrain-surface-detail filtered-detail \
        --terrain-backdrop-azimuth "${heading}"
done

capture envelope-near-low "Envelope: 50 m, 0 deg" camera-envelope \
    --terrain-camera-preset backdrop-stage \
    --terrain-surface-detail filtered-detail \
    --terrain-backdrop-orbit-radius 50 \
    --terrain-backdrop-elevation 0
capture envelope-far-low "Envelope: 250 m, 0 deg" camera-envelope \
    --terrain-camera-preset backdrop-stage \
    --terrain-surface-detail filtered-detail \
    --terrain-backdrop-orbit-radius 250 \
    --terrain-backdrop-elevation 0
capture envelope-far-high "Envelope: 250 m, 30 deg" camera-envelope \
    --terrain-camera-preset backdrop-stage \
    --terrain-surface-detail filtered-detail \
    --terrain-backdrop-orbit-radius 250 \
    --terrain-backdrop-elevation 30

for diagnostic in height slope clay classification-normal material-weights material-normal \
    projected-edge stage-ownership; do
    capture "diagnostic-${diagnostic}" "Diagnostic: ${diagnostic}" diagnostics \
        --terrain-camera-preset backdrop \
        --terrain-surface-detail filtered-detail \
        --debug-view "${diagnostic}"
done

MANIFEST_PATH="${HEIGHTFIELD}"
if [[ -d "${HEIGHTFIELD}" ]]; then
    MANIFEST_PATH="${HEIGHTFIELD}/heightfield.json"
fi
GIT_REVISION="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
jq -n \
    --arg schema "cubey.terrain.product-review.v1" \
    --arg git_revision "${GIT_REVISION}" \
    --arg executable "${APP}" \
    --arg heightfield_manifest "${MANIFEST_PATH}" \
    --arg elevation_sha256 "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" \
    --argjson capture_count "${#CAPTURE_FILES[@]}" \
    '{
        schema: $schema,
        git_revision: $git_revision,
        executable: $executable,
        heightfield_manifest: $heightfield_manifest,
        elevation_sha256: $elevation_sha256,
        resolution: {width: $width, height: $height},
        capture_count: $capture_count
    }' >"${OUT_DIR}/review-metadata.json"

if command -v magick >/dev/null 2>&1; then
    montage_inputs=()
    for index in "${!CAPTURE_FILES[@]}"; do
        montage_inputs+=("-label" "${CAPTURE_LABELS[${index}]}" "${CAPTURE_FILES[${index}]}")
    done
    magick montage "${montage_inputs[@]}" -geometry 320x180+8+26 -tile 4x \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'Terrain product review written to %s\n' "${OUT_DIR}"
