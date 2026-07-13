#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
SOURCE_REPORT_APP="${2:-./build/dev/projects/terrain/terrain_source_report}"
BACKDROP_REPORT_APP="${3:-./build/dev/projects/terrain/terrain_backdrop_report}"
OUT_DIR="${4:-outputs/terrain/source-v3-hierarchy}"
EXPECTED_V1_SHA256="5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb"
EXPECTED_V2_SHA256="c9b1f9b94d7f2d14f8f301df59c29651207c279b43f31339815e552421b2b456"

for executable in "${APP}" "${SOURCE_REPORT_APP}" "${BACKDROP_REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain source v3 review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in jq montage sha256sum awk; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain source v3 review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/far-field" "${OUT_DIR}/components" \
  "${OUT_DIR}/source-shape" "${OUT_DIR}/midground" "${OUT_DIR}/profile" \
  "${OUT_DIR}/reports"

capture() {
  local output="$1"
  local version="$2"
  local seed="$3"
  local camera="$4"
  local view="$5"
  local weathering="${6:-off}"
  local width="${7:-960}"
  local height="${8:-540}"
  "${APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering "${weathering}" --terrain-render-path quality \
    --terrain-source-version "${version}" --terrain-surface-detail layered \
    --terrain-target-edge-px 4 --terrain-camera-preset "${camera}" \
    --terrain-presentation backdrop --debug-view "${view}" \
    --sun-elevation 22 --sun-azimuth -55 --validation
}

far_field_images=()
for seed in 0 9012 12345; do
  for version in v2 v3; do
    for view in surface clay; do
      output="${OUT_DIR}/far-field/seed-${seed}-${version}-${view}.png"
      capture "${output}" "${version}" "${seed}" backdrop "${view}" off 1280 720
      far_field_images+=("${output}")
    done
  done
done

component_images=()
for seed in 0 9012 12345; do
  for component in range massif valley ridge meso; do
    output="${OUT_DIR}/components/seed-${seed}-${component}.png"
    capture "${output}" v3 "${seed}" top "source-${component}" off 640 360
    component_images+=("${output}")
  done
done

shape_images=()
for version in v2 v3; do
  for camera in top oblique; do
    output="${OUT_DIR}/source-shape/seed-12345-${version}-${camera}-clay.png"
    capture "${output}" "${version}" 12345 "${camera}" clay off
    shape_images+=("${output}")
  done
done

midground_images=()
for seed in 9012 12345; do
  output="${OUT_DIR}/midground/seed-${seed}-v3-clay.png"
  capture "${output}" v3 "${seed}" midground clay off 1280 720
  midground_images+=("${output}")
done

montage -label '%t' "${far_field_images[@]}" -tile 4x3 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-source-v3-far-field-sheet.png"
montage -label '%t' "${component_images[@]}" -tile 5x3 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-source-v3-component-sheet.png"
montage -label '%t' "${shape_images[@]}" -tile 2x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-source-v3-shape-sheet.png"
montage -label '%t' "${midground_images[@]}" -tile 2x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-source-v3-midground-sheet.png"

"${SOURCE_REPORT_APP}" > "${OUT_DIR}/reports/source-v1-summary.json"
"${SOURCE_REPORT_APP}" --source-version v2 > "${OUT_DIR}/reports/source-v2-summary.json"
"${SOURCE_REPORT_APP}" --source-version v3 > "${OUT_DIR}/reports/source-v3-summary.json"
"${BACKDROP_REPORT_APP}" --source-version v2 > \
  "${OUT_DIR}/reports/backdrop-camera-v2-summary.json"
"${BACKDROP_REPORT_APP}" --source-version v3 > \
  "${OUT_DIR}/reports/backdrop-camera-v3-summary.json"

v1_sha256="$(sha256sum "${OUT_DIR}/reports/source-v1-summary.json" | awk '{print $1}')"
v2_sha256="$(sha256sum "${OUT_DIR}/reports/source-v2-summary.json" | awk '{print $1}')"
if [[ "${v1_sha256}" != "${EXPECTED_V1_SHA256}" || \
      "${v2_sha256}" != "${EXPECTED_V2_SHA256}" ]]; then
  printf 'terrain source v3 review: source hash mismatch: v1=%s v2=%s\n' \
    "${v1_sha256}" "${v2_sha256}" >&2
  exit 1
fi

if ! jq -e 'all(.summaries[];
    .relief_m >= 1800 and .relief_m <= 4500 and
    .mean_slope < 0.60 and
    .components.range_support_coverage >= 0.15 and
    .components.range_support_coverage <= 0.85 and
    .components.massif_rms_m > 0 and
    (.components.valley_rms_m / .components.massif_rms_m) >= 0.01 and
    (.components.valley_rms_m / .components.massif_rms_m) < 0.10 and
    (.components.ridge_rms_m / .components.massif_rms_m) < 0.18 and
    (.components.meso_rms_m / .components.massif_rms_m) < 0.05)' \
    "${OUT_DIR}/reports/source-v3-summary.json" >/dev/null; then
  printf 'terrain source v3 review: source component acceptance failed\n' >&2
  exit 1
fi

camera_max_occluded_rays="$(jq '[.plans[].near_frame_occluded_ray_count] | max' \
  "${OUT_DIR}/reports/backdrop-camera-v3-summary.json")"
camera_plan_count="$(jq '.plans | length' \
  "${OUT_DIR}/reports/backdrop-camera-v3-summary.json")"
if [[ "${camera_max_occluded_rays}" -gt 2 || "${camera_plan_count}" -ne 6 ]]; then
  printf 'terrain source v3 review: camera contract failed: plans=%s max_occluded=%s\n' \
    "${camera_plan_count}" "${camera_max_occluded_rays}" >&2
  exit 1
fi

"${APP}" --headless --capture video --frames 60 --fps 30 --width 960 --height 540 \
  --output "${OUT_DIR}/profile/quality-layered-v3.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering off \
  --terrain-render-path quality --terrain-source-version v3 \
  --terrain-surface-detail layered --terrain-target-edge-px 4 \
  --terrain-camera-preset backdrop --terrain-presentation backdrop \
  --debug-view surface --sun-elevation 22 --sun-azimuth -55 \
  --profile-output "${OUT_DIR}/profile/quality-layered-v3" \
  --profile-warmup-frames 5 --validation

observed_frame_ms="$(awk -F, '
  $3 == "headless.before_frame" {
    if (count > 0) total += $4 - previous
    previous = $4
    count += 1
  }
  END { if (count > 1) printf "%.4f", total / (count - 1); else print "0" }
' "${OUT_DIR}/profile/quality-layered-v3.passes.csv")"
if ! awk -v frame_ms="${observed_frame_ms}" \
  'BEGIN { exit !(frame_ms > 0.0 && frame_ms < 33.3) }'; then
  printf 'terrain source v3 review: frame budget failed: %s ms\n' \
    "${observed_frame_ms}" >&2
  exit 1
fi

cat > "${OUT_DIR}/review-metadata.json" <<EOF
{
  "schema": "cubey.terrain.source-v3-hierarchy-review.v1",
  "candidate": {"source_version": "v3", "render_path": "quality", "surface_detail": "layered"},
  "control": {"source_version": "v2", "render_path": "quality", "surface_detail": "layered"},
  "source_sha256": {"v1": "${v1_sha256}", "v2": "${v2_sha256}"},
  "seeds": [0, 9012, 12345],
  "camera": {"plan_count": ${camera_plan_count}, "maximum_occluded_rays": ${camera_max_occluded_rays}},
  "observed_frame_interval_ms": ${observed_frame_ms},
  "acceptance": {
    "relief_m": [1800, 4500],
    "maximum_mean_slope": 0.60,
    "range_support_coverage": [0.15, 0.85],
    "valley_to_massif_rms": [0.01, 0.10],
    "maximum_ridge_to_massif_rms": 0.18,
    "maximum_meso_to_massif_rms": 0.05,
    "frame_budget_ms": 33.3
  },
  "known_limitations": [
    "Midground diagnostics still expose overly rounded slopes and broad parallel shoulder bands.",
    "Local weathering is deferred for v3 because direct neighbor resampling exceeds the frame budget.",
    "Source v3 remains an opt-in mountain-only candidate; v1 remains the default."
  ]
}
EOF

printf 'terrain source v3 hierarchy review written to %s\n' "${OUT_DIR}"
