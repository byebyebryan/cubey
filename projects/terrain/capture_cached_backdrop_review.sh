#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
OUT_DIR="${2:-outputs/terrain/cached-backdrop-v1}"

if [[ ! -x "${APP}" ]]; then
  printf 'terrain cached backdrop review: executable not found: %s\n' "${APP}" >&2
  exit 2
fi
for command in ffmpeg jq montage sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain cached backdrop review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/azimuth" "${OUT_DIR}/diagnostics" \
  "${OUT_DIR}/envelope" "${OUT_DIR}/profile" "${OUT_DIR}/video"

common_args=(
  --terrain-preset mountain
  --terrain-weathering local
  --terrain-camera-preset backdrop
  --terrain-backdrop-mode detached
  --terrain-backdrop-min-distance 3200
  --terrain-presentation backdrop
  --sun-elevation 30
  --sun-azimuth -55
  --validation
)

capture() {
  local output="$1"
  local seed="$2"
  local azimuth="$3"
  local radius="$4"
  local elevation="$5"
  local view="$6"
  local camera="${7:-backdrop}"
  "${APP}" --headless --frames 1 --width 2560 --height 1440 \
    --output "${output}" --terrain-seed "${seed}" \
    --terrain-camera-preset "${camera}" \
    --terrain-backdrop-azimuth "${azimuth}" \
    --terrain-backdrop-orbit-radius "${radius}" \
    --terrain-backdrop-elevation "${elevation}" --debug-view "${view}" \
    "${common_args[@]:0:4}" "${common_args[@]:6}"
}

orbit() {
  local seed="$1"
  local profile_prefix="$2"
  local video="${OUT_DIR}/video/seed-${seed}-full-orbit.mp4"
  local profile_args=()
  if [[ -n "${profile_prefix}" ]]; then
    profile_args=(--profile-output "${profile_prefix}" --profile-warmup-frames 30)
  fi
  "${APP}" --headless --capture video --frames 180 --fps 30 \
    --width 2560 --height 1440 --output "${video}" --terrain-seed "${seed}" \
    --debug-view surface "${common_args[@]}" "${profile_args[@]}"

  local frames=(0 30 60 90 120 150)
  local azimuths=(0 60 120 180 240 300)
  for index in "${!frames[@]}"; do
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${frames[index]})" -frames:v 1 \
      "${OUT_DIR}/azimuth/seed-${seed}-azimuth-${azimuths[index]}.png"
  done
}

for seed in 0 9012 12345; do
  profile_prefix=""
  if [[ "${seed}" == "9012" ]]; then
    profile_prefix="${OUT_DIR}/profile/seed-9012-full-orbit"
  fi
  orbit "${seed}" "${profile_prefix}"
done

for seed in 0 9012 12345; do
  images=()
  for azimuth in 0 60 120 180 240 300; do
    images+=("${OUT_DIR}/azimuth/seed-${seed}-azimuth-${azimuth}.png")
  done
  montage -label '%t' "${images[@]}" -tile 3x2 -geometry 640x360+8+24 \
    "${OUT_DIR}/terrain-cached-backdrop-seed-${seed}.png"
done

envelope_specs=("50 0 0" "50 0 180" "100 8 90" "100 8 270" "250 30 0" "250 30 180")
envelope_images=()
for spec in "${envelope_specs[@]}"; do
  read -r radius elevation azimuth <<<"${spec}"
  output="${OUT_DIR}/envelope/radius-${radius}-elevation-${elevation}-azimuth-${azimuth}.png"
  capture "${output}" 9012 "${azimuth}" "${radius}" "${elevation}" surface backdrop-stage
  envelope_images+=("${output}")
done
montage -label '%t' "${envelope_images[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-cached-backdrop-envelope.png"

diagnostic_images=()
for view in clay normal material-weights projected-edge stage-ownership; do
  output="${OUT_DIR}/diagnostics/seed-9012-${view}.png"
  capture "${output}" 9012 0 100 8 "${view}"
  diagnostic_images+=("${output}")
done
montage -label '%t' "${diagnostic_images[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-cached-backdrop-diagnostics.png"

setup_start_ns="$(date +%s%N)"
"${APP}" --headless --frames 1 --width 640 --height 360 \
  --output "${OUT_DIR}/profile/setup-and-first-frame.png" --terrain-seed 9012 \
  --debug-view surface "${common_args[@]}" &
setup_pid=$!
setup_max_rss_kib=0
while kill -0 "${setup_pid}" 2>/dev/null; do
  if [[ -r "/proc/${setup_pid}/status" ]]; then
    setup_rss_kib="$(awk '$1 == "VmHWM:" { print $2 }' "/proc/${setup_pid}/status")"
    if [[ -n "${setup_rss_kib}" ]] && (( setup_rss_kib > setup_max_rss_kib )); then
      setup_max_rss_kib="${setup_rss_kib}"
    fi
  fi
  sleep 0.05
done
wait "${setup_pid}"
setup_end_ns="$(date +%s%N)"
setup_elapsed_ms="$(( (setup_end_ns - setup_start_ns) / 1000000 ))"

passes_csv="${OUT_DIR}/profile/seed-9012-full-orbit.passes.csv"
durations="${OUT_DIR}/profile/terrain-surface-durations.txt"
awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' "${passes_csv}" | \
  sort -n > "${durations}"
sample_count="$(wc -l < "${durations}" | tr -d ' ')"
if (( sample_count < 120 )); then
  printf 'terrain cached backdrop review: expected 120 GPU samples, found %s\n' \
    "${sample_count}" >&2
  exit 1
fi
p95_rank="$(( (95 * sample_count + 99) / 100 ))"
p95_ms="$(sed -n "${p95_rank}p" "${durations}")"
if ! awk -v p95_ms="${p95_ms}" 'BEGIN { exit !(p95_ms < 1.0) }'; then
  printf 'terrain cached backdrop review: terrain surface p95 failed: %s ms\n' \
    "${p95_ms}" >&2
  exit 1
fi

jq -n \
  --argjson setup_elapsed_ms "${setup_elapsed_ms}" \
  --argjson setup_max_rss_kib "${setup_max_rss_kib}" \
  --argjson terrain_surface_gpu_sample_count "${sample_count}" \
  --argjson terrain_surface_gpu_p95_ms "${p95_ms}" \
  '{
    schema: "cubey.terrain.cached-backdrop-review.v1",
    source: {preset: "mountain", version: "v2.1", weathering: "local"},
    product: {
      focus: "fixed",
      consumer_radius_m: 300,
      visible_inner_radius_m: 3200,
      outer_radius_m: 16384,
      density: "high",
      angular_intervals: 3072,
      hidden_radial_intervals: 64,
      visible_radial_intervals: 768,
      sectors: 48,
      source_samples: 2558976,
      source_triangles: 4718592,
      render_triangle_capacity: 540672
    },
    review: {
      resolution: [2560, 1440],
      seeds: [0, 9012, 12345],
      relative_azimuth_degrees: [0, 60, 120, 180, 240, 300],
      orbit_radius_m: [50, 100, 250],
      orbit_elevation_degrees: [0, 8, 30],
      yaw_restricted: false,
      diagnostics: ["clay", "normal", "material-weights", "projected-edge", "stage-ownership"]
    },
    performance: {
      gpu: "NVIDIA GeForce RTX 5070 Ti",
      resolution: [2560, 1440],
      warmup_frames: 30,
      terrain_surface_gpu_sample_count: $terrain_surface_gpu_sample_count,
      terrain_surface_gpu_p95_ms: $terrain_surface_gpu_p95_ms,
      terrain_surface_gpu_p95_limit_ms: 1.0,
      excluded_passes: ["terrain atmosphere", "terrain post", "terrain stage proxy", "capture"]
    },
    setup_and_first_frame: {
      resolution: [640, 360],
      elapsed_ms: $setup_elapsed_ms,
      process_max_rss_kib: $setup_max_rss_kib,
      scope: "process startup, stage search, cached product build, GPU upload, and one headless frame"
    }
  }' > "${OUT_DIR}/review-metadata.json"

printf 'terrain cached backdrop review: wrote %s (terrain p95 %s ms, %s samples)\n' \
  "${OUT_DIR}" "${p95_ms}" "${sample_count}"
