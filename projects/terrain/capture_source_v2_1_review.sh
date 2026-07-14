#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
SOURCE_REPORT_APP="${2:-./build/dev/projects/terrain/terrain_source_report}"
OUT_DIR="${3:-outputs/terrain/source-v2-1}"
EXPECTED_V1_SHA256="5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb"
EXPECTED_V2_SHA256="c9b1f9b94d7f2d14f8f301df59c29651207c279b43f31339815e552421b2b456"

for executable in "${APP}" "${SOURCE_REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain source v2.1 review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in jq montage sha256sum awk; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain source v2.1 review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/far-field" "${OUT_DIR}/shape" "${OUT_DIR}/midground" \
  "${OUT_DIR}/profile" "${OUT_DIR}/reports"

capture() {
  local output="$1"
  local version="$2"
  local seed="$3"
  local camera="$4"
  local view="$5"
  local width="${6:-1280}"
  local height="${7:-720}"
  "${APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering off --terrain-render-path quality \
    --terrain-source-version "${version}" --terrain-surface-detail layered \
    --terrain-target-edge-px 4 --terrain-camera-preset "${camera}" \
    --terrain-presentation backdrop --debug-view "${view}" \
    --sun-elevation 22 --sun-azimuth -55 --validation
}

far_field_images=()
for seed in 0 9012 12345; do
  for version in v2 v2.1; do
    for view in surface clay; do
      label="${version//./-}"
      output="${OUT_DIR}/far-field/seed-${seed}-${label}-${view}.png"
      capture "${output}" "${version}" "${seed}" backdrop "${view}"
      far_field_images+=("${output}")
    done
  done
done

shape_images=()
for seed in 0 9012 12345; do
  for camera in top oblique; do
    for version in v2 v2.1; do
      label="${version//./-}"
      output="${OUT_DIR}/shape/seed-${seed}-${label}-${camera}-clay.png"
      capture "${output}" "${version}" "${seed}" "${camera}" clay 960 540
      shape_images+=("${output}")
    done
  done
done

midground_images=()
for seed in 9012 12345; do
  for version in v2 v2.1; do
    for view in surface clay; do
      label="${version//./-}"
      output="${OUT_DIR}/midground/seed-${seed}-${label}-${view}.png"
      capture "${output}" "${version}" "${seed}" midground "${view}"
      midground_images+=("${output}")
    done
  done
done

montage -label '%t' "${far_field_images[@]}" -tile 4x3 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-source-v2-1-far-field-sheet.png"
montage -label '%t' "${shape_images[@]}" -tile 4x3 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-source-v2-1-shape-sheet.png"
montage -label '%t' "${midground_images[@]}" -tile 4x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-source-v2-1-midground-sheet.png"

"${SOURCE_REPORT_APP}" > "${OUT_DIR}/reports/source-v1-summary.json"
"${SOURCE_REPORT_APP}" --source-version v2 > \
  "${OUT_DIR}/reports/source-v2-summary.json"
"${SOURCE_REPORT_APP}" --source-version v2.1 > \
  "${OUT_DIR}/reports/source-v2-1-summary.json"
"${SOURCE_REPORT_APP}" --source-version v2 --scale-response > \
  "${OUT_DIR}/reports/source-v2-scale-summary.json"
"${SOURCE_REPORT_APP}" --source-version v2.1 --scale-response > \
  "${OUT_DIR}/reports/source-v2-1-scale-summary.json"

v1_sha256="$(sha256sum "${OUT_DIR}/reports/source-v1-summary.json" | awk '{print $1}')"
v2_sha256="$(sha256sum "${OUT_DIR}/reports/source-v2-summary.json" | awk '{print $1}')"
if [[ "${v1_sha256}" != "${EXPECTED_V1_SHA256}" || \
      "${v2_sha256}" != "${EXPECTED_V2_SHA256}" ]]; then
  printf 'terrain source v2.1 review: source hash mismatch: v1=%s v2=%s\n' \
    "${v1_sha256}" "${v2_sha256}" >&2
  exit 1
fi

jq -n \
  --slurpfile control "${OUT_DIR}/reports/source-v2-scale-summary.json" \
  --slurpfile candidate "${OUT_DIR}/reports/source-v2-1-scale-summary.json" '
  {
    schema: "cubey.terrain.source-v2-1-comparison.v1",
    comparisons: [
      $control[0].summaries[] as $v2 |
      $candidate[0].summaries[] |
      select(.seed == $v2.seed) |
      {
        seed,
        relief_ratio: (.relief_m / $v2.relief_m),
        mean_slope_ratio: (.mean_slope / $v2.mean_slope),
        fine_residual_ratio: (.scale_response.fine_residual_rms_m /
          $v2.scale_response.fine_residual_rms_m),
        meso_residual_ratio: (.scale_response.meso_residual_rms_m /
          $v2.scale_response.meso_residual_rms_m),
        structure_residual_ratio: (.scale_response.structure_residual_rms_m /
          $v2.scale_response.structure_residual_rms_m)
      }
    ]
  }
' > "${OUT_DIR}/reports/source-comparison.json"

if ! jq -e 'all(.comparisons[];
    .relief_ratio >= 0.92 and .relief_ratio <= 1.08 and
    .mean_slope_ratio >= 0.65 and .mean_slope_ratio <= 1.00 and
    .fine_residual_ratio >= 0.35 and .fine_residual_ratio <= 0.90 and
    .meso_residual_ratio >= 0.999 and .meso_residual_ratio <= 1.001 and
    .structure_residual_ratio >= 0.999 and .structure_residual_ratio <= 1.001)' \
    "${OUT_DIR}/reports/source-comparison.json" >/dev/null; then
  printf 'terrain source v2.1 review: scale response acceptance failed\n' >&2
  cat "${OUT_DIR}/reports/source-comparison.json" >&2
  exit 1
fi

for version in v2 v2.1; do
  label="${version//./-}"
  "${APP}" --headless --capture video --frames 60 --fps 30 --width 960 --height 540 \
    --output "${OUT_DIR}/profile/quality-layered-${label}.mp4" \
    --terrain-seed 9012 --terrain-preset mountain --terrain-weathering off \
    --terrain-render-path quality --terrain-source-version "${version}" \
    --terrain-surface-detail layered --terrain-target-edge-px 4 \
    --terrain-camera-preset backdrop --terrain-presentation backdrop \
    --debug-view surface --sun-elevation 22 --sun-azimuth -55 \
    --profile-output "${OUT_DIR}/profile/quality-layered-${label}" \
    --profile-warmup-frames 5 --validation
done

frame_interval() {
  awk -F, '
    $3 == "headless.before_frame" {
      if (count > 0) total += $4 - previous
      previous = $4
      count += 1
    }
    END { if (count > 1) printf "%.4f", total / (count - 1); else print "0" }
  ' "$1"
}

v2_frame_ms="$(frame_interval "${OUT_DIR}/profile/quality-layered-v2.passes.csv")"
v2_1_frame_ms="$(frame_interval "${OUT_DIR}/profile/quality-layered-v2-1.passes.csv")"
frame_ratio="$(awk -v control="${v2_frame_ms}" -v candidate="${v2_1_frame_ms}" \
  'BEGIN { if (control > 0) printf "%.6f", candidate / control; else print "0" }')"
if ! awk -v frame_ms="${v2_1_frame_ms}" -v ratio="${frame_ratio}" \
  'BEGIN { exit !(frame_ms > 0.0 && frame_ms < 33.3 && ratio <= 1.05) }'; then
  printf 'terrain source v2.1 review: frame gate failed: v2=%s v2.1=%s ratio=%s\n' \
    "${v2_frame_ms}" "${v2_1_frame_ms}" "${frame_ratio}" >&2
  exit 1
fi

jq -n \
  --arg v1_sha256 "${v1_sha256}" --arg v2_sha256 "${v2_sha256}" \
  --argjson v2_frame_ms "${v2_frame_ms}" --argjson v2_1_frame_ms "${v2_1_frame_ms}" \
  --argjson frame_ratio "${frame_ratio}" \
  --slurpfile comparison "${OUT_DIR}/reports/source-comparison.json" '
  {
    schema: "cubey.terrain.source-v2-1-review.v1",
    candidate: {source_version: "v2.1", render_path: "quality", surface_detail: "layered"},
    control: {source_version: "v2", render_path: "quality", surface_detail: "layered"},
    seeds: [0, 9012, 12345],
    source_sha256: {v1: $v1_sha256, v2: $v2_sha256},
    scale_response_footprints_m: [0, 64, 256, 1024],
    comparisons: $comparison[0].comparisons,
    performance: {
      v2_frame_interval_ms: $v2_frame_ms,
      v2_1_frame_interval_ms: $v2_1_frame_ms,
      candidate_to_control_ratio: $frame_ratio
    },
    known_limitations: [
      "Source v2.1 does not retune the 218 to 1200 m ridge structure.",
      "Source v1 remains the default; v2.1 is an opt-in mountain-only candidate."
    ]
  }
' > "${OUT_DIR}/review-metadata.json"

printf 'terrain source v2.1 review written to %s\n' "${OUT_DIR}"
