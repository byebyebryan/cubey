#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
BACKDROP_REPORT_APP="${2:-./build/dev/projects/terrain/terrain_backdrop_report}"
OUT_DIR="${3:-outputs/terrain/far-field-v1}"

for executable in "${APP}" "${BACKDROP_REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain far-field v1 review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in jq montage; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain far-field v1 review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/far-field" "${OUT_DIR}/midground-control" \
  "${OUT_DIR}/diagnostics" "${OUT_DIR}/reports"

capture() {
  local output="$1"
  local seed="$2"
  local camera="$3"
  local view="$4"
  local sun_elevation="${5:-22}"
  "${APP}" --headless --frames 1 --width 1920 --height 1080 \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering off --terrain-render-path quality \
    --terrain-source-version v2.1 --terrain-surface-detail layered \
    --terrain-target-edge-px 4 --terrain-camera-preset "${camera}" \
    --terrain-presentation backdrop --debug-view "${view}" \
    --sun-elevation "${sun_elevation}" --sun-azimuth -55 --validation
}

far_field_images=()
for seed in 0 9012 12345; do
  for view in surface clay; do
    output="${OUT_DIR}/far-field/seed-${seed}-${view}.png"
    capture "${output}" "${seed}" backdrop "${view}"
    far_field_images+=("${output}")
  done
done

midground_images=()
for seed in 9012 12345; do
  output="${OUT_DIR}/midground-control/seed-${seed}-surface.png"
  capture "${output}" "${seed}" midground surface
  midground_images+=("${output}")
done

diagnostic_images=()
for view in clay shadow lod; do
  output="${OUT_DIR}/diagnostics/seed-12345-${view}.png"
  sun_elevation=22
  if [[ "${view}" == "clay" || "${view}" == "shadow" ]]; then
    sun_elevation=90
  fi
  capture "${output}" 12345 backdrop "${view}" "${sun_elevation}"
  diagnostic_images+=("${output}")
done

montage -label '%t' "${far_field_images[@]}" -tile 2x3 -geometry 960x540+8+24 \
  "${OUT_DIR}/terrain-far-field-v1-sheet.png"
montage -label '%t' "${midground_images[@]}" -tile 2x1 -geometry 960x540+8+24 \
  "${OUT_DIR}/terrain-far-field-v1-midground-control-sheet.png"
montage -label '%t' "${diagnostic_images[@]}" -tile 3x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-far-field-v1-continuity-sheet.png"

"${BACKDROP_REPORT_APP}" --source-version v2.1 > \
  "${OUT_DIR}/reports/backdrop-camera-v2-1.json"

if ! jq -e '
  [.plans[] | select(.profile == "backdrop")] as $plans |
  ($plans | length) == 3 and
  all($plans[];
    .far_field_contract_satisfied == true and
    .target_distance_m >= 3400 and
    .minimum_target_distance_m >= 3200 and
    .safe_zone_radius_m == 200 and
    .safe_zone_foreground_min_margin_m >= 9.99 and
    .safe_zone_near_frame_test_distance_m == 2400 and
    .safe_zone_near_frame_max_occluded_ray_count == 0 and
    .safe_zone_lower_frame_test_distance_m == 1200 and
    .safe_zone_lower_frame_max_occluded_ray_count <= 2)
' "${OUT_DIR}/reports/backdrop-camera-v2-1.json" >/dev/null; then
  printf 'terrain far-field v1 review: camera contract acceptance failed\n' >&2
  exit 1
fi

jq -n \
  --slurpfile planner "${OUT_DIR}/reports/backdrop-camera-v2-1.json" '
  {
    schema: "cubey.terrain.far-field-v1-review.v1",
    recipe: {
      source_version: "v2.1",
      preset: "mountain",
      weathering: "off",
      render_path: "quality",
      surface_detail: "layered",
      camera: "backdrop",
      presentation: "backdrop",
      resolution: [1920, 1080],
      vertical_fov_degrees: 40,
      target_edge_px: 4
    },
    seeds: [0, 9012, 12345],
    product_boundary: {
      safe_zone_radius_m: 200,
      yaw_half_angle_degrees: 30,
      minimum_target_distance_m: 3200
    },
    planner: [$planner[0].plans[] | select(.profile == "backdrop")],
    negative_control: {
      camera: "midground",
      target_distance_m: 1600,
      supported: false
    },
    continuity_diagnostics: ["clay", "shadow", "lod"]
  }
' > "${OUT_DIR}/review-metadata.json"

printf 'terrain far-field v1 review written to %s\n' "${OUT_DIR}"
