#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STUDY_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_source_study}"
REPORT_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_source_study_report}"
REFERENCE_APP="${3:-${ROOT_DIR}/build/dev-terrain-studies/studies/terrain/shadertoy/terrain_shadertoy}"
OUT_DIR="${4:-${ROOT_DIR}/outputs/terrain/mountains-source-decision-v2}"
REFERENCE_SOURCE="${CUBEY_SHADERTOY_MOUNTAINS_SOURCE:-${ROOT_DIR}/../ref/ShaderToy/mountains.glsl}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${STUDY_APP}" "${REPORT_APP}" "${REFERENCE_APP}"; do
    if [[ ! -x "${executable}" ]]; then
        printf 'mountains source decision: executable not found: %s\n' "${executable}" >&2
        exit 2
    fi
done
if [[ ! -f "${REFERENCE_SOURCE}" ]]; then
    printf 'mountains source decision: reference source not found: %s\n' \
        "${REFERENCE_SOURCE}" >&2
    exit 2
fi
for command in ffmpeg git jq magick realpath sha256sum; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'mountains source decision: %s is required\n' "${command}" >&2
        exit 2
    fi
done

recipes=(control-v2-1 mountains-signed mountains-hierarchy-v2)
seeds=(0 9012 12345)
azimuths=(0 60 120 180 240 300)
frames=(0 15 30 45 60 75)
reference_times=(0 20 40)
reference_diagnostics=(height envelope structure uplift)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/clay" \
    "${TMP_DIR}/raw/reference/components" "${TMP_DIR}/raw/reference/oblique"

for recipe in "${recipes[@]}"; do
    "${REPORT_APP}" --output-dir "${TMP_DIR}/reports/${recipe}" --grid-size 1024 \
        --recipe "${recipe}"
done

capture_orbit() {
    local recipe="$1"
    local seed="$2"
    local lane_dir="${TMP_DIR}/raw/clay/${recipe}/seed-${seed}"
    local video="${lane_dir}/orbit.mp4"
    mkdir -p "${lane_dir}"

    "${STUDY_APP}" --headless --capture video --frames 90 --fps 30 \
        --width 1920 --height 1080 --output "${video}" \
        --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --sun-elevation 30 --sun-azimuth -55 --debug-view clay --validation

    for index in "${!frames[@]}"; do
        ffmpeg -hide_banner -loglevel error -i "${video}" \
            -vf "select=eq(n\\,${frames[index]})" -frames:v 1 \
            "${lane_dir}/azimuth-${azimuths[index]}.png"
    done
    rm "${video}"
}

for recipe in "${recipes[@]}"; do
    for seed in "${seeds[@]}"; do
        capture_orbit "${recipe}" "${seed}"
    done
done

for time in "${reference_times[@]}"; do
    "${REFERENCE_APP}" --headless --width 1920 --height 1080 \
        --output "${TMP_DIR}/raw/reference/oblique/time-${time}.png" \
        --reference-study mountains --reference-render mesh --reference-time "${time}" \
        --reference-mesh-cells 1024 --reference-mesh-surface terrain \
        --reference-normal atlas --reference-shading clay
    for diagnostic in "${reference_diagnostics[@]}"; do
        "${REFERENCE_APP}" --headless --width 1024 --height 1024 \
            --output "${TMP_DIR}/raw/reference/components/time-${time}-${diagnostic}.png" \
            --reference-study mountains --reference-render mesh --reference-time "${time}" \
            --reference-mesh-cells 256 --reference-mesh-surface terrain \
            --reference-normal atlas --reference-shading clay \
            --reference-diagnostic "${diagnostic}"
    done
done

height_inputs=()
slope_inputs=()
for recipe in "${recipes[@]}"; do
    for seed in "${seeds[@]}"; do
        height_inputs+=(
            -label "${recipe} / ${seed}"
            "${TMP_DIR}/reports/${recipe}/fields/${recipe}/seed-${seed}-height.png"
        )
        slope_inputs+=(
            -label "${recipe} / ${seed}"
            "${TMP_DIR}/reports/${recipe}/fields/${recipe}/seed-${seed}-slope.png"
        )
    done
done
magick montage "${height_inputs[@]}" -tile 3x3 -geometry 512x512+8+24 \
    "${TMP_DIR}/candidate-height-contact-sheet.png"
magick montage "${slope_inputs[@]}" -tile 3x3 -geometry 512x512+8+24 \
    "${TMP_DIR}/candidate-slope-contact-sheet.png"

for seed in "${seeds[@]}"; do
    clay_inputs=()
    for recipe in "${recipes[@]}"; do
        for azimuth in "${azimuths[@]}"; do
            clay_inputs+=(
                -label "${recipe} / ${azimuth} deg"
                "${TMP_DIR}/raw/clay/${recipe}/seed-${seed}/azimuth-${azimuth}.png"
            )
        done
    done
    magick montage "${clay_inputs[@]}" -tile 6x3 -geometry 480x270+8+24 \
        "${TMP_DIR}/candidate-clay-seed-${seed}.png"
done

component_inputs=()
for time in "${reference_times[@]}"; do
    for diagnostic in "${reference_diagnostics[@]}"; do
        component_inputs+=(
            -label "time ${time} / ${diagnostic}"
            "${TMP_DIR}/raw/reference/components/time-${time}-${diagnostic}.png"
        )
    done
done
magick montage "${component_inputs[@]}" -tile 4x3 -geometry 384x384+8+24 \
    "${TMP_DIR}/reference-component-contact-sheet.png"

reference_inputs=()
for time in "${reference_times[@]}"; do
    reference_inputs+=(
        -label "Mountains / time ${time}"
        "${TMP_DIR}/raw/reference/oblique/time-${time}.png"
    )
done
magick montage "${reference_inputs[@]}" -tile 3x1 -geometry 640x360+8+24 \
    "${TMP_DIR}/reference-oblique-contact-sheet.png"

jq -s \
    '{schema: "cubey.terrain.mountains-source-decision-report.v2",
      recipes: [.[].recipes[0]]}' \
    "${TMP_DIR}"/reports/*/source-report.json > "${TMP_DIR}/source-summary.json"

jq -n \
    --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    --arg reference_path "$(realpath --relative-to "${ROOT_DIR}" "${REFERENCE_SOURCE}")" \
    --arg reference_sha256 "$(sha256sum "${REFERENCE_SOURCE}" | awk '{print $1}')" \
    --argjson recipes "$(printf '%s\n' "${recipes[@]}" | jq -R . | jq -s .)" \
    --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
    --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" \
    '{
      schema: "cubey.terrain.mountains-source-decision-capture.v2",
      cubey_commit: $commit,
      recipes: $recipes,
      seeds: $seeds,
      azimuth_degrees: $azimuths,
      render: {
        resolution: [1920, 1080],
        camera: "cached-backdrop",
        orbit_radius_m: 100,
        orbit_elevation_degrees: 8,
        mesh_density: "high",
        render_stride: 1,
        shading: "clay"
      },
      external_reference: {
        path: $reference_path,
        sha256: $reference_sha256,
        license: "CC BY-NC-SA 3.0",
        vendored: false,
        times_seconds: [0, 20, 40],
        diagnostics: ["height", "envelope", "structure", "uplift"]
      }
    }' > "${TMP_DIR}/capture-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Mountains Source Decision v2

Review in this order:

1. `reference-component-contact-sheet.png` checks whether broad mass,
   intermediate structure, and positive uplift persist across three translated
   exact-reference windows.
2. `candidate-height-contact-sheet.png` checks broad connectivity, multiple
   masses, valleys, and seed stability.
3. `candidate-slope-contact-sheet.png` exposes thin fins, excessive local
   relief, and high-frequency dominance.
4. The three candidate clay sheets test all six yaw directions through the
   same cached-backdrop renderer. They are silhouette checks, not material
   quality evidence.
5. `reference-oblique-contact-sheet.png` prevents one favorable source camera
   from becoming the target by accident.

Rows in candidate sheets are production v2.1, the old Mountains abstraction,
and the corrected scale-coupled candidate. No candidate is promoted by this
pack. `source-summary.json` contains fixed calibration, relief, slope,
throughput, and stage-plan evidence.

## Result

`mountains-hierarchy-v2` improves substantially over `mountains-signed`, but
its height and clay views remain dominated by rounded ranges without reliable
ridge-to-summit hierarchy. V2.1 retains stronger mountain readability at the
cost of excessive slope density. The corrected candidate is not eligible for
production v4; Mountains remains an external morphology reference rather than
a source to match literally.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'mountains source decision: wrote %s\n' "${OUT_DIR}"
