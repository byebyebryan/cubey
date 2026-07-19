#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRODUCT_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
STUDY_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_natural_raster_study}"
REPORT_APP="${3:-${ROOT_DIR}/build/dev/projects/terrain/terrain_natural_raster_report}"
OUT_DIR="${4:-${ROOT_DIR}/outputs/terrain/raster-backdrop-product-v1}"
FIELD_ROOT="${CUBEY_TERRAIN_HEIGHTFIELD_ROOT:-${ROOT_DIR}/outputs/terrain/.terrain-diffusion-bakeoff-v1-fields}"
EXPORTER="${ROOT_DIR}/projects/terrain/tools/export_heightfield_manifest.py"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${PRODUCT_APP}" "${STUDY_APP}" "${REPORT_APP}" "${EXPORTER}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain raster product review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in awk date ffmpeg jq magick python realpath sed sha256sum sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain raster product review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

seeds=(0 9012 12345)
azimuths=(0 60 120 180 240 300)
orbit_frames=(0 15 30 45 60 75)
envelope_specs=("50 0" "100 8" "250 30")

if [[ ! -f "${FIELD_ROOT}/generation-report.json" ]]; then
  printf 'terrain raster product review: missing generated field report: %s\n' \
    "${FIELD_ROOT}/generation-report.json" >&2
  exit 2
fi

for seed in "${seeds[@]}"; do
  field="${FIELD_ROOT}/fields/seed-${seed}"
  if [[ ! -f "${field}/manifest.json" || ! -f "${field}/elevation.f32" ]]; then
    printf 'terrain raster product review: incomplete source field: %s\n' "${field}" >&2
    exit 2
  fi
  "${EXPORTER}" "${field}" >/dev/null
  if ! jq -e --argjson seed "${seed}" '
    .schema == "cubey.terrain.heightfield.v1" and
    .seed == $seed and
    .grid.width == 2048 and .grid.height == 2048 and
    .grid.sample_spacing_m == 30 and
    .files.elevation.path == "elevation.f32" and
    .files.climate == null
  ' "${field}/heightfield.json" >/dev/null; then
    printf 'terrain raster product review: invalid product manifest for seed %s\n' "${seed}" >&2
    exit 1
  fi
done

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/orbits" "${TMP_DIR}/raw/envelope" \
  "${TMP_DIR}/raw/diagnostics" "${TMP_DIR}/raw/stride" "${TMP_DIR}/profile" \
  "${TMP_DIR}/setup"
ln -s "$(realpath --relative-to "${TMP_DIR}" "${FIELD_ROOT}")" "${TMP_DIR}/fields"

for seed in "${seeds[@]}"; do
  report="${TMP_DIR}/reports/seed-${seed}.json"
  "${REPORT_APP}" --terrain-study-field "${FIELD_ROOT}/fields/seed-${seed}" > "${report}"
  if ! jq -e '
    .schema == "cubey.terrain.natural-raster-stage.v1" and
    .support.centered_search_covered == true and
    .support.natural_selection_covered == true and
    .natural.placement.contract_satisfied == true and
    .natural.stage.contract_satisfied == true and
    .natural.requested_focus_height_m == 500 and
    .natural.stage.minimum_camera_clearance_m >= 10
  ' "${report}" >/dev/null; then
    printf 'terrain raster product review: stage contract failed for seed %s\n' "${seed}" >&2
    exit 1
  fi
done

product_args() {
  local seed="$1"
  printf '%s\n' \
    --terrain-seed "${seed}" \
    --terrain-camera-preset backdrop-stage \
    --terrain-backdrop-profile raster-v1 \
    --terrain-heightfield "${FIELD_ROOT}/fields/seed-${seed}" \
    --terrain-presentation backdrop \
    --sun-elevation 30 \
    --sun-azimuth -55 \
    --validation
}

run_product_png() {
  local output="$1" seed="$2" radius="$3" elevation="$4" azimuth="$5" view="$6"
  local width="${7:-1920}" height="${8:-1080}"
  local -a args=()
  mapfile -t args < <(product_args "${seed}")
  if [[ -n "${azimuth}" ]]; then
    args+=(--terrain-backdrop-azimuth "${azimuth}")
  fi
  "${PRODUCT_APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" "${args[@]}" --terrain-backdrop-orbit-radius "${radius}" \
    --terrain-backdrop-elevation "${elevation}" --debug-view "${view}"
}

run_product_video() {
  local output="$1" seed="$2" radius="$3" elevation="$4" view="$5" frames="$6"
  local width="$7" height="$8" profile_prefix="${9:-}" fps="${10:-30}" warmup="${11:-30}"
  local -a args=() profile_args=()
  mapfile -t args < <(product_args "${seed}")
  if [[ -n "${profile_prefix}" ]]; then
    profile_args=(--profile-output "${profile_prefix}" --profile-warmup-frames "${warmup}")
  fi
  "${PRODUCT_APP}" --headless --capture video --frames "${frames}" --fps "${fps}" \
    --width "${width}" --height "${height}" --output "${output}" "${args[@]}" \
    --terrain-backdrop-orbit-radius "${radius}" --terrain-backdrop-elevation "${elevation}" \
    --debug-view "${view}" "${profile_args[@]}"
}

run_radial_profile_video() {
  local output="$1" profile_prefix="$2"
  "${PRODUCT_APP}" --headless --capture video --frames 300 --fps 120 \
    --width 2560 --height 1440 --output "${output}" --terrain-seed 9012 \
    --terrain-camera-preset backdrop-stage --terrain-backdrop-profile radial-v1 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-presentation backdrop --sun-elevation 30 --sun-azimuth -55 \
    --debug-view surface --validation --profile-output "${profile_prefix}" \
    --profile-warmup-frames 60
}

run_stride_one_png() {
  local output="$1" seed="$2"
  "${STUDY_APP}" --headless --frames 1 --width 1920 --height 1080 --output "${output}" \
    --terrain-seed "${seed}" --terrain-study-field "${FIELD_ROOT}/fields/seed-${seed}" \
    --terrain-camera-preset backdrop-stage --terrain-backdrop-orbit-radius 100 \
    --terrain-backdrop-elevation 8 --terrain-presentation backdrop \
    --natural-center-sampling seam-matched --natural-focus-height 500 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view surface --validation
}

extract_orbit() {
  local video="$1" output_dir="$2"
  mkdir -p "${output_dir}"
  for index in "${!orbit_frames[@]}"; do
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${orbit_frames[index]})" -frames:v 1 \
      "${output_dir}/azimuth-${azimuths[index]}.png"
  done
}

for seed in "${seeds[@]}"; do
  orbit_dir="${TMP_DIR}/raw/orbits/seed-${seed}"
  mkdir -p "${orbit_dir}"
  run_product_video "${orbit_dir}/orbit.mp4" "${seed}" 100 8 surface 90 1920 1080
  extract_orbit "${orbit_dir}/orbit.mp4" "${orbit_dir}"
  rm "${orbit_dir}/orbit.mp4"
done

orbit_inputs=()
for seed in "${seeds[@]}"; do
  for azimuth in "${azimuths[@]}"; do
    orbit_inputs+=(
      -label "seed ${seed} / relative yaw ${azimuth} deg"
      "${TMP_DIR}/raw/orbits/seed-${seed}/azimuth-${azimuth}.png"
    )
  done
done
magick montage "${orbit_inputs[@]}" -tile 6x3 -geometry 480x270+8+24 \
  "${TMP_DIR}/raster-v1-seeds-and-yaw.png"

envelope_inputs=()
for seed in "${seeds[@]}"; do
  for spec in "${envelope_specs[@]}"; do
    read -r radius elevation <<<"${spec}"
    lane="${TMP_DIR}/raw/envelope/seed-${seed}-radius-${radius}-elevation-${elevation}"
    mkdir -p "${lane}"
    run_product_video "${lane}/orbit.mp4" "${seed}" "${radius}" "${elevation}" surface \
      60 1280 720
    for frame in 0 45; do
      facing="showcase"
      [[ "${frame}" == "45" ]] && facing="opposite"
      ffmpeg -hide_banner -loglevel error -i "${lane}/orbit.mp4" \
        -vf "select=eq(n\\,${frame})" -frames:v 1 "${lane}/${facing}.png"
      envelope_inputs+=(
        -label "seed ${seed} / ${radius} m / ${elevation} deg / ${facing}"
        "${lane}/${facing}.png"
      )
    done
    rm "${lane}/orbit.mp4"
  done
done
magick montage "${envelope_inputs[@]}" -tile 6x3 -geometry 426x240+8+24 \
  "${TMP_DIR}/raster-v1-camera-envelope.png"

diagnostic_inputs=()
for view in clay normal projected-edge lod stage-ownership; do
  output="${TMP_DIR}/raw/diagnostics/${view}.png"
  run_product_png "${output}" 12345 100 8 "" "${view}"
  diagnostic_inputs+=( -label "seed 12345 / ${view}" "${output}" )
done
magick montage "${diagnostic_inputs[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${TMP_DIR}/raster-v1-diagnostics.png"

stride_inputs=()
for seed in "${seeds[@]}"; do
  stride_one="${TMP_DIR}/raw/stride/seed-${seed}-stride-1.png"
  run_stride_one_png "${stride_one}" "${seed}"
  stride_inputs+=(
    -label "seed ${seed} / product stride 3"
    "${TMP_DIR}/raw/orbits/seed-${seed}/azimuth-0.png"
    -label "seed ${seed} / reference stride 1"
    "${stride_one}"
  )
done
magick montage "${stride_inputs[@]}" -tile 2x3 -geometry 800x450+8+24 \
  "${TMP_DIR}/raster-v1-stride-comparison.png"

setup_start_ns="$(date +%s%N)"
run_product_png "${TMP_DIR}/setup/first-frame.png" 9012 100 8 "" surface 640 360 &
setup_pid=$!
setup_max_rss_kib=0
while kill -0 "${setup_pid}" 2>/dev/null; do
  if [[ -r "/proc/${setup_pid}/status" ]]; then
    setup_rss_kib="$(awk '$1 == "VmHWM:" { print $2 }' "/proc/${setup_pid}/status" \
      2>/dev/null || true)"
    if [[ -n "${setup_rss_kib}" ]] && (( setup_rss_kib > setup_max_rss_kib )); then
      setup_max_rss_kib="${setup_rss_kib}"
    fi
  fi
  sleep 0.05
done
wait "${setup_pid}"
setup_end_ns="$(date +%s%N)"
setup_elapsed_ms="$(( (setup_end_ns - setup_start_ns) / 1000000 ))"

raster_prefix="${TMP_DIR}/profile/raster-v1"
radial_prefix="${TMP_DIR}/profile/radial-v1"
run_product_video "${TMP_DIR}/profile/raster-v1.mp4" 9012 100 8 surface 300 2560 1440 \
  "${raster_prefix}" 120 60
run_radial_profile_video "${TMP_DIR}/profile/radial-v1.mp4" "${radial_prefix}"
rm "${TMP_DIR}/profile/raster-v1.mp4" "${TMP_DIR}/profile/radial-v1.mp4"

profile_summary() {
  local prefix="$1" output="$2"
  local durations="${prefix}-terrain-surface.txt"
  awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' \
    "${prefix}.passes.csv" | sort -n > "${durations}"
  local count p50_rank p95_rank mean p50 p95
  count="$(wc -l < "${durations}" | tr -d ' ')"
  if (( count < 120 )); then
    printf 'terrain raster product review: expected 120 GPU samples, found %s\n' "${count}" >&2
    exit 1
  fi
  p50_rank="$(( (50 * count + 99) / 100 ))"
  p95_rank="$(( (95 * count + 99) / 100 ))"
  mean="$(awk '{ sum += $1 } END { printf "%.6f", sum / NR }' "${durations}")"
  p50="$(sed -n "${p50_rank}p" "${durations}")"
  p95="$(sed -n "${p95_rank}p" "${durations}")"
  jq -n --argjson samples "${count}" --argjson mean "${mean}" --argjson p50 "${p50}" \
    --argjson p95 "${p95}" \
    '{samples: $samples, mean_ms: $mean, p50_ms: $p50, p95_ms: $p95}' > "${output}"
}

profile_summary "${raster_prefix}" "${TMP_DIR}/profile/raster-summary.json"
profile_summary "${radial_prefix}" "${TMP_DIR}/profile/radial-summary.json"

metric_average() {
  local prefix="$1" name="$2"
  awk -F, -v name="${name}" \
    '$2 == "terrain.backdrop" && $3 == name { sum += $4; count += 1 } \
     END { if (count == 0) exit 1; printf "%.6f", sum / count }' \
    "${prefix}.metrics.csv"
}

render_stride="$(metric_average "${raster_prefix}" render_stride)"
outer_radius_m="$(metric_average "${raster_prefix}" outer_radius_m)"
continuous_center="$(metric_average "${raster_prefix}" continuous_center)"
render_triangles="$(metric_average "${raster_prefix}" product_render_triangles)"
source_samples="$(metric_average "${raster_prefix}" source_samples)"
if ! awk -v stride="${render_stride}" -v radius="${outer_radius_m}" \
  -v center="${continuous_center}" -v triangles="${render_triangles}" \
  -v samples="${source_samples}" '
  BEGIN { exit !(stride == 3 && radius == 16384 && center == 1 &&
                  triangles == 607200 && samples == 2657280) }
'; then
  printf 'terrain raster product review: cached product metrics changed\n' >&2
  exit 1
fi

raster_mean="$(jq -r .mean_ms "${TMP_DIR}/profile/raster-summary.json")"
raster_p50="$(jq -r .p50_ms "${TMP_DIR}/profile/raster-summary.json")"
radial_mean="$(jq -r .mean_ms "${TMP_DIR}/profile/radial-summary.json")"
radial_p50="$(jq -r .p50_ms "${TMP_DIR}/profile/radial-summary.json")"
performance_pass=false
if awk -v rm="${raster_mean}" -v rp="${raster_p50}" -v bm="${radial_mean}" \
  -v bp="${radial_p50}" 'BEGIN { exit !(rm <= bm * 1.10 && rp <= bp * 1.10) }'; then
  performance_pass=true
fi

heightfield_hashes="$(for seed in "${seeds[@]}"; do
  sha256sum "${FIELD_ROOT}/fields/seed-${seed}/heightfield.json" | \
    awk -v seed="${seed}" '{print seed "\t" $1}'
done | jq -Rn '[inputs | split("\t") | {seed: .[0] | tonumber, sha256: .[1]}]')"

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --argjson heightfield_hashes "${heightfield_hashes}" \
  --slurpfile raster_profile "${TMP_DIR}/profile/raster-summary.json" \
  --slurpfile radial_profile "${TMP_DIR}/profile/radial-summary.json" \
  --argjson performance_pass "${performance_pass}" \
  --argjson setup_ms "${setup_elapsed_ms}" \
  --argjson setup_rss "${setup_max_rss_kib}" \
  --argjson render_stride "${render_stride}" \
  --argjson outer_radius_m "${outer_radius_m}" \
  --argjson render_triangles "${render_triangles}" \
  --argjson source_samples "${source_samples}" '
  {
    schema: "cubey.terrain.raster-backdrop-product.v1",
    cubey_commit: $commit,
    heightfields: $heightfield_hashes,
    product: {
      profile: "raster-v1", center: "continuous", center_sampling: "seam-matched",
      render_stride: $render_stride, outer_radius_m: $outer_radius_m,
      center_outer_radius_m: 3200, focus_height_m: 500,
      render_triangle_capacity: $render_triangles, source_samples: $source_samples,
      camera_envelope: {orbit_radius_m: [50, 250], elevation_degrees: [0, 30], yaw_restricted: false}
    },
    coverage: {seeds: [0, 9012, 12345], relative_yaw_degrees: [0, 60, 120, 180, 240, 300]},
    performance: {
      resolution: [2560, 1440], warmup_frames: 60,
      raster_v1: $raster_profile[0], radial_v1: $radial_profile[0],
      maximum_relative_regression: 0.10, relative_gate_passed: $performance_pass,
      sub_1_ms_target_deferred: true
    },
    setup_and_first_frame: {resolution: [640, 360], elapsed_ms: $setup_ms, process_max_rss_kib: $setup_rss},
    boundaries: ["far-field only", "fixed focus", "external asset", "no streaming", "no close terrain"]
  }
' > "${TMP_DIR}/review-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Raster Backdrop Product V1

Review in this order:

1. `raster-v1-seeds-and-yaw.png` checks all three external fields across six
   unrestricted headings. Look for a continuous foreground, stable mountain
   silhouettes, and the absence of rings, spokes, holes, or sector seams.
2. `raster-v1-camera-envelope.png` checks the 50/100/250 m and 0/8/30 degree
   profile positions in showcase and opposite directions for every field.
3. `raster-v1-stride-comparison.png` places product stride 3 above the retained
   stride-1 study reference. Judge far-field silhouette and visible faceting;
   this is not a close-detail acceptance view.
4. `raster-v1-diagnostics.png` isolates clay shape, normals, projected edges,
   LOD, and stage ownership on seed 12345.
5. `review-metadata.json` records exact topology metrics, setup cost, and the
   same-machine raster-v1 versus radial-v1 p50/mean performance gate.

The pack does not alter, filter, mask, or regenerate source heights. It does not
claim translated terrain, close ground, streaming, hydrology, or vegetation.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT

printf 'terrain raster product review: wrote %s\n' "${OUT_DIR}"
printf '  raster GPU mean/p50: %s/%s ms\n' "${raster_mean}" "${raster_p50}"
printf '  radial GPU mean/p50: %s/%s ms\n' "${radial_mean}" "${radial_p50}"
printf '  relative 10%% gate passed: %s\n' "${performance_pass}"
printf '  setup and first frame: %s ms, %s KiB max RSS\n' \
  "${setup_elapsed_ms}" "${setup_max_rss_kib}"
