#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STUDY_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_study}"
PRODUCTION_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
OUT_DIR="${3:-${ROOT_DIR}/outputs/terrain/cached-radial-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${STUDY_APP}" "${PRODUCTION_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain cached radial review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in awk date ffmpeg jq magick sed sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain cached radial review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

recipe="mountains-hierarchy-v2"
seeds=(0 9012 12345)
azimuths=(0 60 120 180 240 300)
orbit_frames=(0 15 30 45 60 75)
profile_labels=(full stride-2 stride-3 production)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw/orbits" "${TMP_DIR}/raw/diagnostics" \
  "${TMP_DIR}/raw/envelope" "${TMP_DIR}/profile" "${TMP_DIR}/setup"

study_stride_args=()
set_study_stride_args() {
  local stride="$1"
  study_stride_args=()
  if [[ -n "${stride}" ]]; then
    study_stride_args=(--radial-render-stride "${stride}")
  fi
}

run_study_video() {
  local output="$1"
  local lane="$2"
  local stride="$3"
  local seed="$4"
  local radius="$5"
  local elevation="$6"
  local frame_count="$7"
  local width="$8"
  local height="$9"
  local profile_prefix="${10:-}"
  local profile_args=()
  set_study_stride_args "${stride}"
  if [[ -n "${profile_prefix}" ]]; then
    profile_args=(--profile-output "${profile_prefix}" --profile-warmup-frames 30)
  fi
  "${STUDY_APP}" --headless --capture video --frames "${frame_count}" --fps 30 \
    --width "${width}" --height "${height}" --output "${output}" \
    --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane "${lane}" "${study_stride_args[@]}" \
    --directional-focus-height 500 --directional-orbit-radius "${radius}" \
    --terrain-camera-preset backdrop-stage --terrain-presentation backdrop \
    --terrain-backdrop-azimuth 0 --terrain-backdrop-elevation "${elevation}" \
    --sun-elevation 30 --sun-azimuth -55 --debug-view surface --validation \
    "${profile_args[@]}"
}

run_study_png() {
  local output="$1"
  local lane="$2"
  local stride="$3"
  local seed="$4"
  local radius="$5"
  local elevation="$6"
  local azimuth="$7"
  local view="$8"
  set_study_stride_args "${stride}"
  "${STUDY_APP}" --headless --frames 1 --width 1920 --height 1080 \
    --output "${output}" --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane "${lane}" "${study_stride_args[@]}" \
    --directional-focus-height 500 --directional-orbit-radius "${radius}" \
    --terrain-camera-preset backdrop-stage --terrain-presentation backdrop \
    --terrain-backdrop-azimuth "${azimuth}" --terrain-backdrop-elevation "${elevation}" \
    --sun-elevation 30 --sun-azimuth -55 --debug-view "${view}" --validation
}

run_production_video() {
  local output="$1"
  local frame_count="$2"
  local width="$3"
  local height="$4"
  local profile_prefix="${5:-}"
  local profile_args=()
  if [[ -n "${profile_prefix}" ]]; then
    profile_args=(--profile-output "${profile_prefix}" --profile-warmup-frames 30)
  fi
  "${PRODUCTION_APP}" --headless --capture video --frames "${frame_count}" --fps 30 \
    --width "${width}" --height "${height}" --output "${output}" \
    --terrain-preset mountain --terrain-weathering local --terrain-seed 9012 \
    --terrain-backdrop-profile hard-cut-v1 \
    --terrain-camera-preset backdrop --terrain-backdrop-mode detached \
    --terrain-backdrop-min-distance 3200 --terrain-presentation backdrop \
    --sun-elevation 30 --sun-azimuth -55 --debug-view surface --validation \
    "${profile_args[@]}"
}

extract_orbit() {
  local video="$1"
  local output_dir="$2"
  mkdir -p "${output_dir}"
  for index in "${!orbit_frames[@]}"; do
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${orbit_frames[index]})" -frames:v 1 \
      "${output_dir}/azimuth-${azimuths[index]}.png"
  done
}

for stride in 2 3; do
  for seed in "${seeds[@]}"; do
    orbit_dir="${TMP_DIR}/raw/orbits/stride-${stride}/seed-${seed}"
    video="${orbit_dir}/orbit.mp4"
    mkdir -p "${orbit_dir}"
    run_study_video "${video}" cached-radial "${stride}" "${seed}" 400 8 90 1920 1080
    extract_orbit "${video}" "${orbit_dir}"
    rm "${video}"
  done
done

full_dir="${TMP_DIR}/raw/orbits/full/seed-9012"
production_dir="${TMP_DIR}/raw/orbits/production/seed-9012"
mkdir -p "${full_dir}" "${production_dir}"
run_study_video "${full_dir}/orbit.mp4" expanded-radial "" 9012 400 8 90 1920 1080
extract_orbit "${full_dir}/orbit.mp4" "${full_dir}"
rm "${full_dir}/orbit.mp4"
run_production_video "${production_dir}/orbit.mp4" 90 1920 1080
extract_orbit "${production_dir}/orbit.mp4" "${production_dir}"
rm "${production_dir}/orbit.mp4"

make_orbit_sheet() {
  local output="$1"
  shift
  local specs=("$@")
  local inputs=()
  for spec in "${specs[@]}"; do
    IFS='|' read -r label key seed <<<"${spec}"
    for azimuth in "${azimuths[@]}"; do
      inputs+=(
        -label "${label} / ${azimuth} deg"
        "${TMP_DIR}/raw/orbits/${key}/seed-${seed}/azimuth-${azimuth}.png"
      )
    done
  done
  magick montage "${inputs[@]}" -tile "6x${#specs[@]}" -geometry 480x270+8+24 \
    "${output}"
}

make_orbit_sheet "${TMP_DIR}/cached-radial-stride-2-seeds.png" \
  "seed 0|stride-2|0" "seed 9012|stride-2|9012" "seed 12345|stride-2|12345"
make_orbit_sheet "${TMP_DIR}/cached-radial-stride-3-seeds.png" \
  "seed 0|stride-3|0" "seed 9012|stride-3|9012" "seed 12345|stride-3|12345"
make_orbit_sheet "${TMP_DIR}/cached-radial-integration-comparison.png" \
  "full radial / stride 1|full|9012" \
  "cached radial / stride 2|stride-2|9012" \
  "cached radial / stride 3|stride-3|9012" \
  "current production hard cut|production|9012"

diagnostic_inputs=()
for view in clay normal projected-edge material-weights stage-ownership; do
  output="${TMP_DIR}/raw/diagnostics/seed-9012-${view}.png"
  run_study_png "${output}" cached-radial 3 9012 400 8 0 "${view}"
  diagnostic_inputs+=( -label "${view}" "${output}" )
done
magick montage "${diagnostic_inputs[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${TMP_DIR}/cached-radial-stride-3-diagnostics.png"

envelope_inputs=()
for spec in "100 0" "400 8" "1000 30"; do
  read -r radius elevation <<<"${spec}"
  envelope_dir="${TMP_DIR}/raw/envelope/radius-${radius}-elevation-${elevation}"
  video="${envelope_dir}/orbit.mp4"
  mkdir -p "${envelope_dir}"
  run_study_video "${video}" cached-radial 3 9012 "${radius}" "${elevation}" 90 1920 1080
  for frame_azimuth in "0 0" "45 180"; do
    read -r frame azimuth <<<"${frame_azimuth}"
    output="${envelope_dir}/azimuth-${azimuth}.png"
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${frame})" -frames:v 1 "${output}"
    envelope_inputs+=(
      -label "radius ${radius} m / elevation ${elevation} deg / yaw ${azimuth} deg"
      "${output}"
    )
  done
  rm "${video}"
done
magick montage "${envelope_inputs[@]}" -tile 2x3 -geometry 720x405+8+24 \
  "${TMP_DIR}/cached-radial-stride-3-camera-envelope.png"

declare -A setup_elapsed_ms setup_max_rss_kib
measure_setup() {
  local label="$1"
  local stride="$2"
  local output="${TMP_DIR}/setup/${label}.png"
  set_study_stride_args "${stride}"
  local start_ns
  start_ns="$(date +%s%N)"
  "${STUDY_APP}" --headless --frames 1 --width 640 --height 360 --output "${output}" \
    --terrain-recipe "${recipe}" --terrain-seed 9012 --directional-lane cached-radial \
    "${study_stride_args[@]}" --directional-focus-height 500 \
    --directional-orbit-radius 400 --terrain-camera-preset backdrop-stage \
    --terrain-presentation backdrop --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view surface --validation &
  local setup_pid=$!
  local max_rss_kib=0
  while kill -0 "${setup_pid}" 2>/dev/null; do
    if [[ -r "/proc/${setup_pid}/status" ]]; then
      local rss_kib
      rss_kib="$(awk '$1 == "VmHWM:" { print $2 }' "/proc/${setup_pid}/status")"
      if [[ -n "${rss_kib}" ]] && (( rss_kib > max_rss_kib )); then
        max_rss_kib="${rss_kib}"
      fi
    fi
    sleep 0.05
  done
  wait "${setup_pid}"
  local end_ns
  end_ns="$(date +%s%N)"
  setup_elapsed_ms["${label}"]="$(( (end_ns - start_ns) / 1000000 ))"
  setup_max_rss_kib["${label}"]="${max_rss_kib}"
}

measure_setup stride-2 2
measure_setup stride-3 3

run_study_video "${TMP_DIR}/profile/full.mp4" expanded-radial "" 9012 400 8 180 \
  2560 1440 "${TMP_DIR}/profile/full"
run_study_video "${TMP_DIR}/profile/stride-2.mp4" cached-radial 2 9012 400 8 180 \
  2560 1440 "${TMP_DIR}/profile/stride-2"
run_study_video "${TMP_DIR}/profile/stride-3.mp4" cached-radial 3 9012 400 8 180 \
  2560 1440 "${TMP_DIR}/profile/stride-3"
run_production_video "${TMP_DIR}/profile/production.mp4" 180 2560 1440 \
  "${TMP_DIR}/profile/production"
rm "${TMP_DIR}/profile/"*.mp4

declare -A profile_samples profile_p95_ms profile_gpu_pass \
  profile_submitted_sectors profile_submitted_triangles \
  profile_render_triangles profile_source_samples
metric_average() {
  local csv="$1"
  local name="$2"
  awk -F, -v name="${name}" \
    '$2 == "terrain.backdrop" && $3 == name { sum += $4; count += 1 } \
     END { if (count == 0) exit 1; printf "%.6f", sum / count }' "${csv}"
}

for label in "${profile_labels[@]}"; do
  prefix="${TMP_DIR}/profile/${label}"
  durations="${prefix}-terrain-surface-durations.txt"
  awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' \
    "${prefix}.passes.csv" | sort -n > "${durations}"
  sample_count="$(wc -l < "${durations}" | tr -d ' ')"
  if (( sample_count < 120 )); then
    printf 'terrain cached radial review: %s expected 120 GPU samples, found %s\n' \
      "${label}" "${sample_count}" >&2
    exit 1
  fi
  p95_rank="$(( (95 * sample_count + 99) / 100 ))"
  p95_ms="$(sed -n "${p95_rank}p" "${durations}")"
  gpu_pass=false
  if awk -v value="${p95_ms}" 'BEGIN { exit !(value < 1.0) }'; then
    gpu_pass=true
  fi
  metrics_csv="${prefix}.metrics.csv"
  profile_samples["${label}"]="${sample_count}"
  profile_p95_ms["${label}"]="${p95_ms}"
  profile_gpu_pass["${label}"]="${gpu_pass}"
  profile_submitted_sectors["${label}"]="$(metric_average "${metrics_csv}" submitted_sectors)"
  profile_submitted_triangles["${label}"]="$(metric_average "${metrics_csv}" submitted_triangles)"
  profile_render_triangles["${label}"]="$(metric_average "${metrics_csv}" product_render_triangles)"
  profile_source_samples["${label}"]="$(metric_average "${metrics_csv}" source_samples)"
done

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --argjson stride2_setup_ms "${setup_elapsed_ms[stride-2]}" \
  --argjson stride2_setup_rss "${setup_max_rss_kib[stride-2]}" \
  --argjson stride3_setup_ms "${setup_elapsed_ms[stride-3]}" \
  --argjson stride3_setup_rss "${setup_max_rss_kib[stride-3]}" \
  --argjson full_samples "${profile_samples[full]}" \
  --argjson full_p95 "${profile_p95_ms[full]}" \
  --argjson full_pass "${profile_gpu_pass[full]}" \
  --argjson full_sectors "${profile_submitted_sectors[full]}" \
  --argjson full_submitted "${profile_submitted_triangles[full]}" \
  --argjson full_capacity "${profile_render_triangles[full]}" \
  --argjson full_source_samples "${profile_source_samples[full]}" \
  --argjson stride2_samples "${profile_samples[stride-2]}" \
  --argjson stride2_p95 "${profile_p95_ms[stride-2]}" \
  --argjson stride2_pass "${profile_gpu_pass[stride-2]}" \
  --argjson stride2_sectors "${profile_submitted_sectors[stride-2]}" \
  --argjson stride2_submitted "${profile_submitted_triangles[stride-2]}" \
  --argjson stride2_capacity "${profile_render_triangles[stride-2]}" \
  --argjson stride2_source_samples "${profile_source_samples[stride-2]}" \
  --argjson stride3_samples "${profile_samples[stride-3]}" \
  --argjson stride3_p95 "${profile_p95_ms[stride-3]}" \
  --argjson stride3_pass "${profile_gpu_pass[stride-3]}" \
  --argjson stride3_sectors "${profile_submitted_sectors[stride-3]}" \
  --argjson stride3_submitted "${profile_submitted_triangles[stride-3]}" \
  --argjson stride3_capacity "${profile_render_triangles[stride-3]}" \
  --argjson stride3_source_samples "${profile_source_samples[stride-3]}" \
  --argjson production_samples "${profile_samples[production]}" \
  --argjson production_p95 "${profile_p95_ms[production]}" \
  --argjson production_pass "${profile_gpu_pass[production]}" \
  --argjson production_sectors "${profile_submitted_sectors[production]}" \
  --argjson production_submitted "${profile_submitted_triangles[production]}" \
  --argjson production_capacity "${profile_render_triangles[production]}" \
  --argjson production_source_samples "${profile_source_samples[production]}" \
  '{
    schema: "cubey.terrain.cached-radial-review.v1",
    cubey_commit: $commit,
    candidate: {
      source: {recipe: "mountains-hierarchy-v2", seeds: [0, 9012, 12345]},
      center_mode: "continuous",
      outer_radius_m: 32768,
      floor_footprint_m: 6000,
      broad_range_m: [1000, 24000],
      detail_range_m: [5000, 30000],
      tested_render_strides: [2, 3]
    },
    production_control: {
      source: {preset: "mountain", version: "v2.1", weathering: "local", seed: 9012},
      center_mode: "cutout",
      outer_radius_m: 16384,
      note: "Current product control; source and stage contract intentionally differ from the stride comparison."
    },
    review: {
      resolution: [1920, 1080],
      relative_azimuth_degrees: [0, 60, 120, 180, 240, 300],
      camera_envelope: [
        {radius_m: 100, elevation_degrees: 0},
        {radius_m: 400, elevation_degrees: 8},
        {radius_m: 1000, elevation_degrees: 30}
      ],
      yaw_restricted: false,
      diagnostics: ["clay", "normal", "projected-edge", "material-weights", "stage-ownership"]
    },
    performance: {
      resolution: [2560, 1440],
      warmup_frames: 30,
      terrain_surface_gpu_p95_limit_ms: 1.0,
      full: {samples: $full_samples, p95_ms: $full_p95, passes: $full_pass, average_submitted_sectors: $full_sectors, average_submitted_triangles: $full_submitted, render_triangle_capacity: $full_capacity, source_samples: $full_source_samples},
      stride_2: {samples: $stride2_samples, p95_ms: $stride2_p95, passes: $stride2_pass, average_submitted_sectors: $stride2_sectors, average_submitted_triangles: $stride2_submitted, render_triangle_capacity: $stride2_capacity, source_samples: $stride2_source_samples},
      stride_3: {samples: $stride3_samples, p95_ms: $stride3_p95, passes: $stride3_pass, average_submitted_sectors: $stride3_sectors, average_submitted_triangles: $stride3_submitted, render_triangle_capacity: $stride3_capacity, source_samples: $stride3_source_samples},
      production: {samples: $production_samples, p95_ms: $production_p95, passes: $production_pass, average_submitted_sectors: $production_sectors, average_submitted_triangles: $production_submitted, render_triangle_capacity: $production_capacity, source_samples: $production_source_samples}
    },
    setup_and_first_frame: {
      resolution: [640, 360],
      stride_2: {elapsed_ms: $stride2_setup_ms, process_max_rss_kib: $stride2_setup_rss},
      stride_3: {elapsed_ms: $stride3_setup_ms, process_max_rss_kib: $stride3_setup_rss},
      scope: "Process startup, stage search, cached product build, GPU upload, and one headless frame."
    },
    decision_policy: [
      "Prefer stride 3 only when it is visually acceptable and terrain surface GPU p95 is below 1 ms.",
      "Otherwise prefer stride 2 only when its visual advantage is material and its GPU p95 is below 1 ms.",
      "Reject cached radial integration when neither candidate satisfies both gates."
    ],
    production_promoted: false
  }' > "${TMP_DIR}/review-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Cached Radial Integration Review

Review in this order:

1. `cached-radial-integration-comparison.png`: compare full radial stride 1,
   cached stride 2, cached stride 3, and the current production hard cut over six
   headings. The first three rows isolate render topology; the production row is
   a current-product reference with a different source and stage contract.
2. `cached-radial-stride-{2,3}-seeds.png`: look for silhouette loss, low-poly
   peaks, sector seams, transition exposure, and seed-specific failure over the
   same unrestricted orbit.
3. `cached-radial-stride-3-camera-envelope.png`: test the accepted 100-1000 m,
   0-30 degree camera envelope at opposing headings.
4. `cached-radial-stride-3-diagnostics.png`: use clay and normals for topology,
   projected edges for triangle scale, material weights for channel continuity,
   and stage ownership for the foreground/background boundary.
5. `review-metadata.json`: compare 1440p terrain-surface GPU p95 and submitted
   work. A missed 1 ms gate is evidence, not a capture-script failure.

This pack does not promote either candidate to production.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT

printf 'terrain cached radial review: wrote %s\n' "${OUT_DIR}"
printf '  full p95:       %s ms\n' "${profile_p95_ms[full]}"
printf '  stride 2 p95:   %s ms (%s)\n' \
  "${profile_p95_ms[stride-2]}" "${profile_gpu_pass[stride-2]}"
printf '  stride 3 p95:   %s ms (%s)\n' \
  "${profile_p95_ms[stride-3]}" "${profile_gpu_pass[stride-3]}"
printf '  production p95: %s ms\n' "${profile_p95_ms[production]}"
