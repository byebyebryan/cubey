#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain_source_study}"
REPORT="${2:-./build/dev/projects/terrain/terrain_source_study_report}"
OUT_DIR="${3:-outputs/terrain/source-model-study-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${APP}" "${REPORT}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain source model study: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in ffmpeg jq montage; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain source model study: %s is required\n' "${command}" >&2
    exit 2
  fi
done

recipes=(
  control-v2-1
  terrain-engine-fbm
  elevated-derivative
  swiss-derivative
  mountains-signed
  rainforest-cliff
  mountain-peak-warp
)
seeds=(0 9012 12345)
azimuths=(0 120 240)
frames=(0 30 60)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw/clay" "${TMP_DIR}/raw/presentation"

"${REPORT}" --output-dir "${TMP_DIR}" --grid-size 1024

capture_orbit() {
  local recipe="$1"
  local seed="$2"
  local view="$3"
  local lane="$4"
  local lane_dir="${TMP_DIR}/raw/${lane}/${recipe}/seed-${seed}"
  local video="${lane_dir}/orbit.mp4"
  mkdir -p "${lane_dir}"

  "${APP}" --headless --capture video --frames 90 --fps 30 \
    --width 1920 --height 1080 --output "${video}" \
    --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view "${view}" --validation

  for index in "${!frames[@]}"; do
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${frames[index]})" -frames:v 1 \
      "${lane_dir}/azimuth-${azimuths[index]}.png"
  done
  rm "${video}"
}

for recipe in "${recipes[@]}"; do
  for seed in "${seeds[@]}"; do
    capture_orbit "${recipe}" "${seed}" clay clay
  done
  capture_orbit "${recipe}" 9012 surface presentation
done

height_inputs=()
slope_inputs=()
for recipe in "${recipes[@]}"; do
  for seed in "${seeds[@]}"; do
    height_inputs+=(
      -label "${recipe} / ${seed}"
      "${TMP_DIR}/fields/${recipe}/seed-${seed}-height.png"
    )
    slope_inputs+=(
      -label "${recipe} / ${seed}"
      "${TMP_DIR}/fields/${recipe}/seed-${seed}-slope.png"
    )
  done
done
montage "${height_inputs[@]}" -tile 3x7 -geometry 384x384+8+24 \
  "${TMP_DIR}/terrain-source-study-height.png"
montage "${slope_inputs[@]}" -tile 3x7 -geometry 384x384+8+24 \
  "${TMP_DIR}/terrain-source-study-slope.png"

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
  montage "${clay_inputs[@]}" -tile 3x7 -geometry 640x360+8+24 \
    "${TMP_DIR}/terrain-source-study-clay-seed-${seed}.png"
done

presentation_inputs=()
for recipe in "${recipes[@]}"; do
  for azimuth in "${azimuths[@]}"; do
    presentation_inputs+=(
      -label "${recipe} / ${azimuth} deg"
      "${TMP_DIR}/raw/presentation/${recipe}/seed-9012/azimuth-${azimuth}.png"
    )
  done
done
montage "${presentation_inputs[@]}" -tile 3x7 -geometry 640x360+8+24 \
  "${TMP_DIR}/terrain-source-study-presentation-seed-9012.png"

jq -n \
  --arg schema "cubey.terrain.source-model-study-capture.v1" \
  --argjson recipes "$(printf '%s\n' "${recipes[@]}" | jq -R . | jq -s .)" \
  --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" \
  '{
    schema: $schema,
    source_report: "source-report.json",
    recipes: $recipes,
    seeds: $seeds,
    azimuth_degrees: $azimuths,
    render: {
      resolution: [1920, 1080],
      camera: "backdrop",
      orbit_radius_m: 100,
      orbit_elevation_degrees: 8,
      backdrop_extent_m: [3200, 16384],
      mesh_density: "high",
      render_stride: 1,
      sun_elevation_degrees: 30,
      sun_azimuth_degrees: -55
    },
    sheets: [
      "terrain-source-study-height.png",
      "terrain-source-study-slope.png",
      "terrain-source-study-clay-seed-0.png",
      "terrain-source-study-clay-seed-9012.png",
      "terrain-source-study-clay-seed-12345.png",
      "terrain-source-study-presentation-seed-9012.png"
    ]
  }' > "${TMP_DIR}/capture-metadata.json"

printf '%s\n' \
  'Terrain source model study v1' \
  '' \
  'Review height first: broad buildup, range continuity, ridge body, summit hierarchy, and repetition.' \
  'Use slope to find thin fins, contour bands, grid directions, and high-frequency noise.' \
  'Use clay to compare silhouettes without material camouflage across all three seeds.' \
  'Use presentation last; it tests compatibility with the common renderer, not source truth.' \
  'No candidate is promoted by this pack.' > "${TMP_DIR}/REVIEW.txt"

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
printf 'terrain source model study: wrote %s\n' "${OUT_DIR}"
