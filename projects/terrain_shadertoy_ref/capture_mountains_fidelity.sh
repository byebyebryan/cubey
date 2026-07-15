#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${1:-${ROOT_DIR}/build/dev/projects/terrain_shadertoy_ref/terrain_shadertoy_ref}"
OUT_DIR="${2:-${ROOT_DIR}/outputs/terrain_shadertoy_ref/mountains-fidelity-v1}"
SOURCE="${CUBEY_SHADERTOY_MOUNTAINS_SOURCE:-${ROOT_DIR}/../ref/ShaderToy/mountains.glsl}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

if [[ ! -x "${APP}" ]]; then
    printf 'Mountains fidelity capture: executable not found: %s\n' "${APP}" >&2
    exit 2
fi
if [[ ! -f "${SOURCE}" ]]; then
    printf 'Mountains fidelity capture: source not found: %s\n' "${SOURCE}" >&2
    exit 2
fi
for command in awk git jq magick realpath sha256sum; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Mountains fidelity capture: %s is required\n' "${command}" >&2
        exit 2
    fi
done

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw/direct" "${TMP_DIR}/raw/mesh" \
    "${TMP_DIR}/raw/topology" "${TMP_DIR}/raw/ablations" \
    "${TMP_DIR}/raw/diagnostics"

capture_scene() {
    local output="$1"
    shift
    "${APP}" --headless --width 1920 --height 1080 --output "${output}" "$@"
}

times=(0 20 40)
for time in "${times[@]}"; do
    capture_scene "${TMP_DIR}/raw/direct/time-${time}.png" \
        --reference-render raymarch --reference-time "${time}"
    capture_scene "${TMP_DIR}/raw/mesh/time-${time}.png" \
        --reference-render mesh --reference-time "${time}" --reference-mesh-cells 1024
done

for cells in 256 512 1024; do
    capture_scene "${TMP_DIR}/raw/topology/cells-${cells}.png" \
        --reference-render mesh --reference-time 20 --reference-mesh-cells "${cells}" \
        --reference-mesh-surface map --reference-normal geometry \
        --reference-shading clay
done

capture_scene "${TMP_DIR}/raw/ablations/map-detailed-clay.png" \
    --reference-render mesh --reference-time 20 --reference-mesh-cells 1024 \
    --reference-mesh-surface map --reference-normal detailed --reference-shading clay
capture_scene "${TMP_DIR}/raw/ablations/map-geometry-clay.png" \
    --reference-render mesh --reference-time 20 --reference-mesh-cells 1024 \
    --reference-mesh-surface map --reference-normal geometry --reference-shading clay
capture_scene "${TMP_DIR}/raw/ablations/terrain-detailed-original.png" \
    --reference-render mesh --reference-time 20 --reference-mesh-cells 1024 \
    --reference-mesh-surface terrain --reference-normal detailed \
    --reference-shading original
capture_scene "${TMP_DIR}/raw/ablations/terrain-detailed-clay.png" \
    --reference-render mesh --reference-time 20 --reference-mesh-cells 1024 \
    --reference-mesh-surface terrain --reference-normal detailed --reference-shading clay

for diagnostic in height slope; do
    "${APP}" --headless --width 1024 --height 1024 \
        --output "${TMP_DIR}/raw/diagnostics/terrain-${diagnostic}.png" \
        --reference-render mesh --reference-time 20 --reference-mesh-cells 256 \
        --reference-mesh-surface terrain --reference-diagnostic "${diagnostic}"
done

fidelity_inputs=()
for time in "${times[@]}"; do
    fidelity_inputs+=(
        -label "direct / time ${time}"
        "${TMP_DIR}/raw/direct/time-${time}.png"
        -label "mesh 1024 / time ${time}"
        "${TMP_DIR}/raw/mesh/time-${time}.png"
    )
done
magick montage "${fidelity_inputs[@]}" -tile 2x3 -geometry 640x360+8+24 \
    "${TMP_DIR}/direct-vs-mesh-contact-sheet.png"

magick montage \
    -label '256 cells' "${TMP_DIR}/raw/topology/cells-256.png" \
    -label '512 cells' "${TMP_DIR}/raw/topology/cells-512.png" \
    -label '1024 cells' "${TMP_DIR}/raw/topology/cells-1024.png" \
    -tile 3x1 -geometry 640x360+8+24 "${TMP_DIR}/topology-contact-sheet.png"

magick montage \
    -label 'map / detailed / original' "${TMP_DIR}/raw/mesh/time-20.png" \
    -label 'map / detailed / clay' "${TMP_DIR}/raw/ablations/map-detailed-clay.png" \
    -label 'map / geometry / clay' "${TMP_DIR}/raw/ablations/map-geometry-clay.png" \
    -label 'terrain / detailed / original' \
        "${TMP_DIR}/raw/ablations/terrain-detailed-original.png" \
    -label 'terrain / detailed / clay' \
        "${TMP_DIR}/raw/ablations/terrain-detailed-clay.png" \
    -tile 3x2 -geometry 640x360+8+24 "${TMP_DIR}/ablation-contact-sheet.png"

magick montage \
    -label 'base Terrain height' "${TMP_DIR}/raw/diagnostics/terrain-height.png" \
    -label 'base Terrain slope' "${TMP_DIR}/raw/diagnostics/terrain-slope.png" \
    -tile 2x1 -geometry 512x512+8+24 "${TMP_DIR}/source-diagnostic-contact-sheet.png"

source_sha256="$(sha256sum "${SOURCE}" | awk '{print $1}')"
cubey_commit="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
source_path="$(realpath --relative-to "${ROOT_DIR}" "${SOURCE}")"
app_path="$(realpath --relative-to "${ROOT_DIR}" "${APP}")"
jq -n \
    --arg schema 'cubey.terrain.shadertoy-mountains-fidelity.v1' \
    --arg source_path "${source_path}" \
    --arg source_sha256 "${source_sha256}" \
    --arg cubey_commit "${cubey_commit}" \
    --arg app "${app_path}" \
    '{
        schema: $schema,
        external_source: {
            path: $source_path,
            sha256: $source_sha256,
            url: "https://www.shadertoy.com/view/4slGD4",
            declared_license: "CC BY-NC-SA 3.0 Unported",
            vendored: false
        },
        cubey_commit: $cubey_commit,
        executable: $app,
        capture_script: "projects/terrain_shadertoy_ref/capture_mountains_fidelity.sh",
        input_substitutions: {
            iChannel0: "deterministic generated RGBA8 hash texture",
            iChannel1: "deterministic generated RGBA8 hash texture",
            iMouse: [0, 0, 0, 0]
        },
        comparison: {
            times_seconds: [0, 20, 40],
            scene_resolution: [1920, 1080],
            diagnostic_resolution: [1024, 1024],
            height_atlas_resolution: [2048, 2048],
            mesh_cells: [256, 512, 1024],
            domain_extent_reference_units: 512,
            full_mesh: {
                surface: "map",
                normal: "detailed",
                shading: "original"
            }
        },
        sheets: [
            "direct-vs-mesh-contact-sheet.png",
            "topology-contact-sheet.png",
            "ablation-contact-sheet.png",
            "source-diagnostic-contact-sheet.png"
        ]
    }' > "${TMP_DIR}/capture-metadata.json"

printf '%s\n' \
    'ShaderToy Mountains fidelity v1' \
    '' \
    '1. Start with direct-vs-mesh: compare silhouette, ridge placement, occlusion, and composition.' \
    '2. Use topology to identify errors caused by mesh density rather than the source.' \
    '3. Use ablations to separate detailed normals, procedural tree displacement, and material.' \
    '4. Use source diagnostics last to inspect the base five-octave Terrain field directly.' \
    '' \
    'This pack is local evidence. The external source and generated SPIR-V are not copied here.' \
    > "${TMP_DIR}/REVIEW.txt"

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
printf 'Mountains fidelity capture: wrote %s\n' "${OUT_DIR}"
