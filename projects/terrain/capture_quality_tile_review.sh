#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
REPORT_APP="${2:-./build/dev/projects/terrain/terrain_backdrop_stage_report}"
OUT_DIR="${3:-outputs/terrain/quality-tile-v1}"

for executable in "${APP}" "${REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain quality tile review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in ffprobe jq montage magick; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain quality tile review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/azimuth" "${OUT_DIR}/diagnostics" "${OUT_DIR}/envelope" \
  "${OUT_DIR}/native" "${OUT_DIR}/profile" "${OUT_DIR}/reports" \
  "${OUT_DIR}/seeds"

capture() {
  local output="$1"
  local camera="$2"
  local seed="$3"
  local azimuth="$4"
  local radius="$5"
  local elevation="$6"
  local view="$7"
  local width="${8:-960}"
  local height="${9:-540}"
  "${APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering local --terrain-render-path quality \
    --terrain-source-version v2.1 --terrain-surface-detail layered \
    --terrain-target-edge-px 4 --terrain-camera-preset "${camera}" \
    --terrain-backdrop-mode detached --terrain-backdrop-min-distance 3200 \
    --terrain-backdrop-azimuth "${azimuth}" --terrain-backdrop-orbit-radius "${radius}" \
    --terrain-backdrop-elevation "${elevation}" --terrain-presentation backdrop \
    --debug-view "${view}" --sun-elevation 30 --sun-azimuth -55 --validation
}

azimuths=(0 60 120 180 240 300)
azimuth_pairs=()
for azimuth in "${azimuths[@]}"; do
  clean="${OUT_DIR}/azimuth/seed-9012-azimuth-${azimuth}-clean.png"
  stage="${OUT_DIR}/azimuth/seed-9012-azimuth-${azimuth}-stage.png"
  capture "${clean}" backdrop 9012 "${azimuth}" 100 0 surface
  capture "${stage}" backdrop-stage 9012 "${azimuth}" 100 0 surface
  azimuth_pairs+=("${clean}" "${stage}")
done
montage -label '%t' "${azimuth_pairs[@]}" -tile 4x3 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-quality-tile-azimuths.png"

seed_images=()
for seed in 0 9012 12345; do
  output="${OUT_DIR}/seeds/seed-${seed}-clean.png"
  capture "${output}" backdrop "${seed}" 0 100 8 surface
  seed_images+=("${output}")
done
montage -label '%t' "${seed_images[@]}" -tile 3x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-quality-tile-seeds.png"

envelope_images=()
envelope_specs=("50 0 0" "50 0 180" "100 8 90" "100 8 270" "250 30 0" "250 30 180")
for spec in "${envelope_specs[@]}"; do
  read -r radius elevation azimuth <<<"${spec}"
  output="${OUT_DIR}/envelope/radius-${radius}-elevation-${elevation}-azimuth-${azimuth}.png"
  capture "${output}" backdrop-stage 9012 "${azimuth}" "${radius}" "${elevation}" surface
  envelope_images+=("${output}")
done
montage -label '%t' "${envelope_images[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-quality-tile-envelope.png"

diagnostic_images=()
for view in clay tessellation-factor projected-edge stage-ownership; do
  output="${OUT_DIR}/diagnostics/seed-9012-${view}.png"
  capture "${output}" backdrop-stage 9012 0 100 0 "${view}"
  diagnostic_images+=("${output}")
done
montage -label '%t' "${diagnostic_images[@]}" -tile 2x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-quality-tile-diagnostics.png"

native_images=()
for seed in 0 9012 12345; do
  output="${OUT_DIR}/native/seed-${seed}-1920x1080.png"
  capture "${output}" backdrop "${seed}" 0 100 8 surface 1920 1080
  native_images+=("${output}")
done
montage -label '%t' "${native_images[@]}" -tile 3x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-quality-tile-native.png"

"${APP}" --headless --capture video --frames 120 --fps 24 --width 960 --height 540 \
  --output "${OUT_DIR}/profile/seed-9012-full-orbit.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-render-path quality --terrain-source-version v2.1 \
  --terrain-surface-detail layered --terrain-target-edge-px 4 \
  --terrain-camera-preset backdrop --terrain-backdrop-mode detached \
  --terrain-backdrop-min-distance 3200 --terrain-presentation backdrop \
  --debug-view surface --sun-elevation 30 --sun-azimuth -55 --validation \
  --profile-output "${OUT_DIR}/profile/quality-tile" --profile-warmup-frames 5

"${REPORT_APP}" > "${OUT_DIR}/reports/backdrop-stage.json"
if ! jq -e '
  [.plans[] | select(.mode == "detached")] as $detached |
  ($detached | length) == 3 and
  all($detached[];
    .contract_satisfied == true and
    .minimum_lower_frame_terrain_distance_m >= 3200 and
    .stage_radius_m == 300)
' "${OUT_DIR}/reports/backdrop-stage.json" >/dev/null; then
  printf 'terrain quality tile review: backdrop stage contract failed\n' >&2
  exit 1
fi

video_duration="$(ffprobe -v error -show_entries format=duration \
  -of default=noprint_wrappers=1:nokey=1 \
  "${OUT_DIR}/profile/seed-9012-full-orbit.mp4")"
incremental_frame_ms="$(awk -F, '
  $3 == "headless.before_frame" {
    if (count > 0) total += $4 - previous
    previous = $4
    count += 1
  }
  END { if (count > 1) printf "%.4f", total / (count - 1); else print "0" }
' "${OUT_DIR}/profile/quality-tile.passes.csv")"
if ! awk -v frame_ms="${incremental_frame_ms}" \
  'BEGIN { exit !(frame_ms > 0.0 && frame_ms < 33.3) }'; then
  printf 'terrain quality tile review: frame budget failed: %s ms\n' \
    "${incremental_frame_ms}" >&2
  exit 1
fi

jq -n \
  --arg schema "cubey.terrain.quality-tile-review.v1" \
  --argjson video_duration_seconds "${video_duration}" \
  --argjson incremental_frame_ms "${incremental_frame_ms}" \
  '{schema: $schema,
    source: {preset: "mountain", version: "v2.1", weathering: "local"},
    geometry: {path: "world-aligned-tiles", half_extent_m: 16384,
      patches_per_axis: 128, patch_count: 16384, maximum_patch_span_m: 256},
    quality: {target_edge_px: 4, maximum_tessellation_factor: 64},
    orbit: {radius_m: [50, 100, 250], elevation_degrees: [0, 8, 30],
      yaw_restricted: false},
    ownership: {detached_radius_m: 300, diagnostic: "stage-ownership"},
    native_resolution: [1920, 1080],
    profile: {resolution: [960, 540], frames: 120, warmup_frames: 5,
      incremental_frame_ms: $incremental_frame_ms},
    video_duration_seconds: $video_duration_seconds}' \
  > "${OUT_DIR}/review-metadata.json"

printf 'terrain quality tile review: wrote %s\n' "${OUT_DIR}"
