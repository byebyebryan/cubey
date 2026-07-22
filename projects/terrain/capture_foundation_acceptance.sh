#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TERRAIN_APP="${TERRAIN_APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
GLTF_APP="${GLTF_APP:-${ROOT_DIR}/build/dev/projects/gltf_viewer/gltf_viewer}"
PNG_STATS="${PNG_STATS:-${ROOT_DIR}/build/dev/cubey_png_stats}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/build/dev/assets/terrain/default/heightfield.json}"
OUT_DIR="${1:-${ROOT_DIR}/docs/evidence/terrain-backdrop-foundation}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-360}"

for executable in "${TERRAIN_APP}" "${GLTF_APP}" "${PNG_STATS}"; do
    if [[ ! -x "${executable}" ]]; then
        printf 'required executable not found: %s\n' "${executable}" >&2
        printf '%s\n' \
            'Build cubey_project_terrain, cubey_project_gltf_viewer, and cubey_png_stats first.' \
            >&2
        exit 1
    fi
done
if [[ ! -f "${HEIGHTFIELD}" ]]; then
    printf 'terrain heightfield manifest not found: %s\n' "${HEIGHTFIELD}" >&2
    printf '%s\n' \
        'Generate it with: cmake --build --preset dev --target cubey_terrain_generate_default_asset' \
        >&2
    exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
    printf 'jq is required to write the acceptance manifest\n' >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

TERRAIN_OUTPUT="${OUT_DIR}/terrain.png"
GLTF_OUTPUT="${OUT_DIR}/gltf-viewer.png"
COMMON_ENVIRONMENT_ARGS=(
    --time-of-day-mode manual
    --sun-elevation 38
    --sun-azimuth -42
    --pause-time
)

"${TERRAIN_APP}" \
    --headless \
    --width "${WIDTH}" \
    --height "${HEIGHT}" \
    --terrain-heightfield "${HEIGHTFIELD}" \
    --terrain-placement selected \
    --terrain-camera-preset backdrop-stage \
    --terrain-foreground-height 200 \
    --terrain-surface-detail filtered-detail \
    --terrain-shadows \
    "${COMMON_ENVIRONMENT_ARGS[@]}" \
    --output "${TERRAIN_OUTPUT}"

"${GLTF_APP}" \
    --headless \
    --width "${WIDTH}" \
    --height "${HEIGHT}" \
    --terrain-heightfield "${HEIGHTFIELD}" \
    --terrain-foreground-height 200 \
    --pbr-environment-source atmosphere \
    "${COMMON_ENVIRONMENT_ARGS[@]}" \
    --output "${GLTF_OUTPUT}"

TERRAIN_STATS_RAW="$("${PNG_STATS}" "${TERRAIN_OUTPUT}" 0.02 0.05)"
GLTF_STATS_RAW="$("${PNG_STATS}" "${GLTF_OUTPUT}" 0.02 0.05)"
printf '%s\n%s\n' "${TERRAIN_STATS_RAW}" "${GLTF_STATS_RAW}"
TERRAIN_STATS="${TERRAIN_STATS_RAW/${TERRAIN_OUTPUT}/terrain.png}"
GLTF_STATS="${GLTF_STATS_RAW/${GLTF_OUTPUT}/gltf-viewer.png}"

GIT_REVISION="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
ELEVATION_SHA256="$(jq -r '.files.elevation.sha256' "${HEIGHTFIELD}")"
TERRAIN_SHA256="$(sha256sum "${TERRAIN_OUTPUT}" | awk '{print $1}')"
GLTF_SHA256="$(sha256sum "${GLTF_OUTPUT}" | awk '{print $1}')"

jq -n \
    --arg schema "cubey.terrain.backdrop-foundation-evidence.v1" \
    --arg git_revision "${GIT_REVISION}" \
    --arg heightfield "${HEIGHTFIELD#"${ROOT_DIR}/"}" \
    --arg elevation_sha256 "${ELEVATION_SHA256}" \
    --arg terrain_sha256 "${TERRAIN_SHA256}" \
    --arg gltf_sha256 "${GLTF_SHA256}" \
    --arg terrain_stats "${TERRAIN_STATS}" \
    --arg gltf_stats "${GLTF_STATS}" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" \
    '{
        schema: $schema,
        captured_revision: $git_revision,
        resolution: {width: $width, height: $height},
        source: {
            manifest: $heightfield,
            elevation_sha256: $elevation_sha256
        },
        captures: [
            {
                file: "terrain.png",
                consumer: "projects/terrain",
                sha256: $terrain_sha256,
                validation: $terrain_stats
            },
            {
                file: "gltf-viewer.png",
                consumer: "projects/gltf_viewer",
                sha256: $gltf_sha256,
                validation: $gltf_stats
            }
        ],
        fixed_contract: {
            placement: "selected",
            foreground_height_m: 200,
            surface_detail: "filtered-detail",
            terrain_shadows: true,
            environment: "atmosphere",
            sun_elevation_degrees: 38,
            sun_azimuth_degrees: -42
        }
    }' >"${OUT_DIR}/manifest.json"

printf 'Terrain foundation evidence written to %s\n' "${OUT_DIR}"
