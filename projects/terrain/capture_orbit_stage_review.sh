#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
REPORT_APP="${2:-./build/dev/projects/terrain/terrain_backdrop_stage_report}"
OUT_DIR="${3:-outputs/terrain/orbit-stage-v1}"

for executable in "${APP}" "${REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain orbit stage review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in ffprobe jq montage; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain orbit stage review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/detached" "${OUT_DIR}/grounded" \
  "${OUT_DIR}/ownership-envelope" "${OUT_DIR}/low-sun-control" \
  "${OUT_DIR}/reports" "${OUT_DIR}/video"

capture() {
  local output="$1"
  local seed="$2"
  local mode="$3"
  shift 3
  "${APP}" --headless --frames 1 --width 960 --height 540 \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering off --terrain-render-path quality \
    --terrain-source-version v2.1 --terrain-surface-detail layered \
    --terrain-target-edge-px 4 --terrain-camera-preset backdrop \
    --terrain-backdrop-mode "${mode}" --terrain-presentation backdrop \
    --debug-view surface --sun-elevation 30 --sun-azimuth -55 --validation "$@"
}

azimuths=(0 45 90 135 180 225 270 315)
seed_sheets=()
for seed in 0 9012 12345; do
  images=()
  for azimuth in "${azimuths[@]}"; do
    output="${OUT_DIR}/detached/seed-${seed}-azimuth-${azimuth}.png"
    capture "${output}" "${seed}" detached --terrain-backdrop-azimuth "${azimuth}"
    images+=("${output}")
  done
  sheet="${OUT_DIR}/detached/seed-${seed}-orbit-sheet.png"
  montage -label '%t' "${images[@]}" -tile 4x2 -geometry 480x270+8+24 "${sheet}"
  seed_sheets+=("${sheet}")
done
montage -label '%t' "${seed_sheets[@]}" -tile 1x3 -geometry 960x540+8+24 \
  "${OUT_DIR}/terrain-orbit-stage-detached-sheet.png"

grounded_images=()
for seed in 0 9012 12345; do
  output="${OUT_DIR}/grounded/seed-${seed}.png"
  capture "${output}" "${seed}" grounded
  grounded_images+=("${output}")
done
montage -label '%t' "${grounded_images[@]}" -tile 3x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-orbit-stage-grounded-sheet.png"

envelope_images=()
for azimuth in 0 90 180 270; do
  output="${OUT_DIR}/ownership-envelope/seed-12345-azimuth-${azimuth}.png"
  capture "${output}" 12345 detached --terrain-backdrop-azimuth "${azimuth}" \
    --terrain-backdrop-orbit-radius 150 --terrain-backdrop-elevation 12
  envelope_images+=("${output}")
done
montage -label '%t' "${envelope_images[@]}" -tile 2x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-orbit-stage-ownership-envelope-sheet.png"

"${APP}" --headless --frames 1 --width 960 --height 540 \
  --output "${OUT_DIR}/low-sun-control/seed-12345-azimuth-90-sun-22.png" \
  --terrain-seed 12345 --terrain-preset mountain --terrain-weathering off \
  --terrain-render-path quality --terrain-source-version v2.1 \
  --terrain-surface-detail layered --terrain-target-edge-px 4 \
  --terrain-camera-preset backdrop --terrain-backdrop-mode detached \
  --terrain-backdrop-azimuth 90 --terrain-presentation backdrop \
  --debug-view surface --sun-elevation 22 --sun-azimuth -55 --validation

"${APP}" --headless --capture video --frames 240 --fps 24 --width 960 --height 540 \
  --output "${OUT_DIR}/video/seed-9012-full-orbit.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering off \
  --terrain-render-path quality --terrain-source-version v2.1 \
  --terrain-surface-detail layered --terrain-target-edge-px 4 \
  --terrain-camera-preset backdrop --terrain-backdrop-mode detached \
  --terrain-presentation backdrop --debug-view surface \
  --sun-elevation 30 --sun-azimuth -55 --validation

"${REPORT_APP}" > "${OUT_DIR}/reports/backdrop-stage.json"
if ! jq -e '
  [.plans[] | select(.mode == "detached")] as $detached |
  [.plans[] | select(.mode == "grounded")] as $grounded |
  ($detached | length) == 3 and
  all($detached[];
    .contract_satisfied == true and
    .panorama_sector_count == 24 and
    .lower_frame_clear_sector_count == 24 and
    .minimum_lower_frame_terrain_distance_m >= 3200 and
    .relief_sector_count >= 14 and
    .stage_radius_m == 300) and
  ($grounded | length) == 3 and
  all($grounded[];
    (.local_relief_m | type) == "number" and
    (.local_p95_slope | type) == "number")
' "${OUT_DIR}/reports/backdrop-stage.json" >/dev/null; then
  printf 'terrain orbit stage review: stage contract failed\n' >&2
  exit 1
fi

video_duration="$(ffprobe -v error -show_entries format=duration \
  -of default=noprint_wrappers=1:nokey=1 "${OUT_DIR}/video/seed-9012-full-orbit.mp4")"
jq -n \
  --arg schema "cubey.terrain.orbit-stage-review.v1" \
  --argjson seeds '[0, 9012, 12345]' \
  --argjson azimuths '[0, 45, 90, 135, 180, 225, 270, 315]' \
  --argjson video_duration_seconds "${video_duration}" \
  '{schema: $schema, seeds: $seeds, detached_azimuths_degrees: $azimuths,
    source: {preset: "mountain", version: "v2.1", weathering: "off"},
    orbit: {radius_m: [50, 100, 150], detached_elevation_degrees: [4, 8, 12],
      grounded_elevation_degrees: [12, 20, 32], yaw_restricted: false},
    ownership: {detached_radius_m: 300},
    minimum_visible_terrain_distance_m: 3200,
    known_negative_control: {sun_elevation_degrees: 22,
      issue: "sparse heightfield-shadow contour bands"},
    video_duration_seconds: $video_duration_seconds}' \
  > "${OUT_DIR}/review-metadata.json"

printf 'terrain orbit stage review: wrote %s\n' "${OUT_DIR}"
