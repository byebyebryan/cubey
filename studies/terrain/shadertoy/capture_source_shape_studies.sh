#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${1:-${ROOT_DIR}/build/dev-terrain-studies/studies/terrain/shadertoy/terrain_shadertoy}"
OUT_DIR="${2:-${ROOT_DIR}/outputs/terrain_shadertoy_ref/source-shape-studies-v1}"
SOURCE_DIR="${CUBEY_SHADERTOY_REF_DIR:-${ROOT_DIR}/../ref/ShaderToy}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

if [[ ! -x "${APP}" ]]; then
    printf 'Source-shape capture: executable not found: %s\n' "${APP}" >&2
    exit 2
fi
for command in git jq magick realpath sha256sum; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Source-shape capture: %s is required\n' "${command}" >&2
        exit 2
    fi
done

source_files=(
    mountains.glsl
    swiss_alps_common.glsl
    swiss_alps_buffer_b.glsl
    mountain_peak.glsl
    eroded_mountains_common.glsl
    eroded_mountains_buffer_b.glsl
)
for source_file in "${source_files[@]}"; do
    if [[ ! -f "${SOURCE_DIR}/${source_file}" ]]; then
        printf 'Source-shape capture: source not found: %s\n' \
            "${SOURCE_DIR}/${source_file}" >&2
        exit 2
    fi
done

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw/oblique" "${TMP_DIR}/raw/diagnostic"

studies=(mountains swiss-alps mountain-peak erosion-filter)
yaws=(0 90)

capture_oblique() {
    local study="$1"
    local yaw="$2"
    local output="${TMP_DIR}/raw/oblique/${study}-yaw-${yaw}.png"
    "${APP}" --headless --width 1920 --height 1080 --output "${output}" \
        --reference-study "${study}" --reference-render mesh --reference-time 20 \
        --reference-yaw-offset-deg "${yaw}" --reference-mesh-cells 1024 \
        --reference-mesh-surface terrain --reference-normal atlas \
        --reference-shading clay
}

capture_diagnostic() {
    local study="$1"
    local diagnostic="$2"
    local output="${TMP_DIR}/raw/diagnostic/${study}-${diagnostic}.png"
    "${APP}" --headless --width 1024 --height 1024 --output "${output}" \
        --reference-study "${study}" --reference-render mesh --reference-time 20 \
        --reference-mesh-cells 256 --reference-mesh-surface terrain \
        --reference-normal atlas --reference-shading clay \
        --reference-diagnostic "${diagnostic}"
}

for study in "${studies[@]}"; do
    for yaw in "${yaws[@]}"; do
        capture_oblique "${study}" "${yaw}"
    done
    capture_diagnostic "${study}" height
    capture_diagnostic "${study}" slope
done

oblique_inputs=()
for study in "${studies[@]}"; do
    for yaw in "${yaws[@]}"; do
        oblique_inputs+=(
            -label "${study} / yaw ${yaw}"
            "${TMP_DIR}/raw/oblique/${study}-yaw-${yaw}.png"
        )
    done
done
magick montage "${oblique_inputs[@]}" -tile 2x4 -geometry 768x432+8+24 \
    "${TMP_DIR}/oblique-contact-sheet.png"

diagnostic_inputs=()
for study in "${studies[@]}"; do
    for diagnostic in height slope; do
        diagnostic_inputs+=(
            -label "${study} / ${diagnostic}"
            "${TMP_DIR}/raw/diagnostic/${study}-${diagnostic}.png"
        )
    done
done
magick montage "${diagnostic_inputs[@]}" -tile 2x4 -geometry 512x512+8+24 \
    "${TMP_DIR}/diagnostic-contact-sheet.png"

source_entries="${TMP_DIR}/source-entries.jsonl"
: > "${source_entries}"
for source_file in "${source_files[@]}"; do
    source_path="${SOURCE_DIR}/${source_file}"
    jq -n \
        --arg path "$(realpath --relative-to "${ROOT_DIR}" "${source_path}")" \
        --arg sha256 "$(sha256sum "${source_path}" | awk '{print $1}')" \
        '{path: $path, sha256: $sha256, vendored: false}' >> "${source_entries}"
done

jq -n \
    --arg cubey_commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    --arg executable "$(realpath --relative-to "${ROOT_DIR}" "${APP}")" \
    --slurpfile sources "${source_entries}" \
    '{
        schema: "cubey.terrain.shadertoy-source-shape-studies.v1",
        cubey_commit: $cubey_commit,
        executable: $executable,
        capture_script: "studies/terrain/shadertoy/capture_source_shape_studies.sh",
        sources: $sources,
        capture: {
            time_seconds: 20,
            oblique: {resolution: [1920, 1080], cells: 1024, yaws_degrees: [0, 90]},
            diagnostic: {resolution: [1024, 1024], modes: ["height", "slope"]},
            surface: "terrain",
            normals: "atlas",
            shading: "clay",
            height_atlas: {extent: [2048, 2048], format: "RGBA32F"}
        },
        studies: {
            mountains: {
                role: "direct control",
                source: "mountains.glsl",
                license: "CC BY-NC-SA 3.0",
                adaptation: "five-octave Terrain field transferred without source-shape changes"
            },
            "swiss-alps": {
                role: "global source candidate",
                sources: ["swiss_alps_common.glsl", "swiss_alps_buffer_b.glsl"],
                license: "not declared in archived bundle; audit only",
                adaptation: "MQ geometry height and HQ normal-detail height; no original renderer"
            },
            "mountain-peak": {
                role: "focused morphology audit",
                source: "mountain_peak.glsl",
                license: "CC BY-NC-SA 3.0",
                adaptation: "geometry and fragment heights retain explicit radial attenuation"
            },
            "erosion-filter": {
                role: "process modifier audit",
                sources: ["eroded_mountains_common.glsl", "eroded_mountains_buffer_b.glsl"],
                license: "filter sections MPL-2.0; surrounding bundle external",
                adaptation: "reference filter applied to normalized Mountains height and slope, then blended at 25 percent"
            }
        }
    }' > "${TMP_DIR}/manifest.json"

cat > "${TMP_DIR}/README.md" <<'EOF'
# ShaderToy Source-Shape Studies v1

This pack compares source morphology through one mesh, camera, clay material,
and diagnostic harness. It is not a presentation-quality renderer comparison.

Start with `oblique-contact-sheet.png`: compare broad mass, peak buildup,
shoulders, ridge width, repetition, and whether each source survives a 90-degree
view. Then use `diagnostic-contact-sheet.png` to distinguish actual height
structure from lighting. Height is normalized per study to its declared review
range; slope uses each study's fixed threshold.

- `mountains` is the direct control that motivated this pass.
- `swiss-alps` is a global derivative-damped field, but its archived source has
  no clear reusable license.
- `mountain-peak` deliberately retains a radial focal mask and is audit-only.
- `erosion-filter` is a 25% modifier over Mountains. Full reference strength
  overwhelmed the base with narrow fins, so this lane tests selective use only.

`manifest.json` records exact source hashes, adaptations, and licensing notes.
All source code remains external under `../ref/ShaderToy`.

## Result

Mountains remains the strongest global source in this set. Swiss Alps exposes
a cellular uplift network and isolated peaks; Mountain Peak produces a strong
hero silhouette but its radial focus fails the side view; and the erosion pass
adds far more narrow slope detail than useful macro structure. These are study
results, not production source selections.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'Source-shape capture: wrote %s\n' "${OUT_DIR}"
