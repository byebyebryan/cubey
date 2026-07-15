#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
REPORT_APP="${2:-./build/dev/projects/terrain/terrain_backdrop_stage_report}"
OUT_DIR="${3:-outputs/terrain/midair-stage-v1}"

for executable in "${APP}" "${REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain mid-air stage review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in ffprobe jq montage; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain mid-air stage review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/clean" "${OUT_DIR}/stage" "${OUT_DIR}/paired" \
  "${OUT_DIR}/ownership-envelope" "${OUT_DIR}/reports" "${OUT_DIR}/video"

capture() {
  local output="$1"
  local camera="$2"
  local seed="$3"
  local azimuth="$4"
  shift 4
  "${APP}" --headless --frames 1 --width 960 --height 540 \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering off --terrain-render-path quality \
    --terrain-source-version v2.1 --terrain-surface-detail layered \
    --terrain-target-edge-px 4 --terrain-camera-preset "${camera}" \
    --terrain-backdrop-mode detached --terrain-backdrop-min-distance 1500 \
    --terrain-backdrop-azimuth "${azimuth}" --terrain-presentation backdrop \
    --debug-view surface --sun-elevation 30 --sun-azimuth -55 --validation "$@"
}

azimuths=(0 45 90 135 180 225 270 315)
seed_sheets=()
for seed in 0 9012 12345; do
  paired_images=()
  for azimuth in "${azimuths[@]}"; do
    clean="${OUT_DIR}/clean/seed-${seed}-azimuth-${azimuth}.png"
    stage="${OUT_DIR}/stage/seed-${seed}-azimuth-${azimuth}.png"
    capture "${clean}" backdrop "${seed}" "${azimuth}"
    capture "${stage}" backdrop-stage "${seed}" "${azimuth}"
    paired_images+=("${clean}" "${stage}")
  done
  sheet="${OUT_DIR}/paired/seed-${seed}-clean-stage-sheet.png"
  montage -label '%t' "${paired_images[@]}" -tile 4x4 -geometry 480x270+8+24 "${sheet}"
  seed_sheets+=("${sheet}")
done
montage -label '%t' "${seed_sheets[@]}" -tile 1x3 -geometry 960x1080+8+24 \
  "${OUT_DIR}/terrain-midair-stage-all-seeds.png"

envelope_images=()
for azimuth in 0 90 180 270; do
  output="${OUT_DIR}/ownership-envelope/seed-12345-azimuth-${azimuth}.png"
  capture "${output}" backdrop-stage 12345 "${azimuth}" \
    --terrain-backdrop-orbit-radius 250 --terrain-backdrop-elevation 30
  envelope_images+=("${output}")
done
montage -label '%t' "${envelope_images[@]}" -tile 2x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-midair-stage-ownership-envelope.png"

"${APP}" --headless --capture video --frames 240 --fps 24 --width 960 --height 540 \
  --output "${OUT_DIR}/video/seed-9012-stage-full-orbit.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering off \
  --terrain-render-path quality --terrain-source-version v2.1 \
  --terrain-surface-detail layered --terrain-target-edge-px 4 \
  --terrain-camera-preset backdrop-stage --terrain-backdrop-mode detached \
  --terrain-backdrop-min-distance 1500 --terrain-presentation backdrop \
  --debug-view surface --sun-elevation 30 --sun-azimuth -55 --validation

"${REPORT_APP}" > "${OUT_DIR}/reports/backdrop-stage.json"
if ! jq -e '
  [.plans[] | select(.mode == "detached")] as $detached |
  ($detached | length) == 3 and
  all($detached[];
    .contract_satisfied == true and
    .panorama_sector_count == 24 and
    .lower_frame_clear_sector_count == 24 and
    .minimum_lower_frame_terrain_distance_m >= 1500 and
    .relief_sector_count >= 14 and
    .stage_radius_m == 300)
' "${OUT_DIR}/reports/backdrop-stage.json" >/dev/null; then
  printf 'terrain mid-air stage review: stage contract failed\n' >&2
  exit 1
fi

video_duration="$(ffprobe -v error -show_entries format=duration \
  -of default=noprint_wrappers=1:nokey=1 \
  "${OUT_DIR}/video/seed-9012-stage-full-orbit.mp4")"
jq -n \
  --arg schema "cubey.terrain.midair-stage-review.v1" \
  --argjson seeds '[0, 9012, 12345]' \
  --argjson azimuths '[0, 45, 90, 135, 180, 225, 270, 315]' \
  --argjson video_duration_seconds "${video_duration}" \
  '{schema: $schema, seeds: $seeds, azimuths_degrees: $azimuths,
    source: {preset: "mountain", version: "v2.1", weathering: "off"},
    views: ["backdrop", "backdrop-stage"],
    orbit: {radius_m: [50, 100, 250], elevation_degrees: [0, 8, 30],
      yaw_restricted: false},
    ownership: {detached_radius_m: 300},
    minimum_visible_terrain_distance_m: 1500,
    proxy: {sphere_radius_m: 20},
    video_duration_seconds: $video_duration_seconds}' \
  > "${OUT_DIR}/review-metadata.json"

printf 'terrain mid-air stage review: wrote %s\n' "${OUT_DIR}"
