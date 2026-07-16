#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRODUCT_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
STUDY_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_study}"
OUT_DIR="${3:-${ROOT_DIR}/outputs/terrain/radial-backdrop-product-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${PRODUCT_APP}" "${STUDY_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain radial product review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in awk date ffmpeg jq magick sed sha256sum sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain radial product review: %s is required\n' "${command}" >&2
    exit 2
  fi
done

seeds=(0 9012 12345)
azimuths=(0 60 120 180 240 300)
orbit_frames=(0 15 30 45 60 75)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw/orbits" "${TMP_DIR}/raw/centers" \
  "${TMP_DIR}/raw/parity" "${TMP_DIR}/raw/envelope" \
  "${TMP_DIR}/raw/diagnostics" "${TMP_DIR}/profile" "${TMP_DIR}/setup"

center_args=()
set_center_args() {
  local center="$1"
  center_args=()
  if [[ -n "${center}" ]]; then
    center_args=(--terrain-backdrop-center "${center}")
  fi
}

run_product_video() {
  local output="$1"
  local seed="$2"
  local center="$3"
  local frame_count="$4"
  local width="$5"
  local height="$6"
  local profile_prefix="${7:-}"
  local fps="${8:-30}"
  local warmup_frames="${9:-30}"
  local profile_args=()
  set_center_args "${center}"
  if [[ -n "${profile_prefix}" ]]; then
    profile_args=(--profile-output "${profile_prefix}" \
      --profile-warmup-frames "${warmup_frames}")
  fi
  "${PRODUCT_APP}" --headless --capture video --frames "${frame_count}" --fps "${fps}" \
    --width "${width}" --height "${height}" --output "${output}" \
    --terrain-seed "${seed}" "${center_args[@]}" \
    --terrain-camera-preset backdrop-stage --terrain-backdrop-orbit-radius 400 \
    --terrain-backdrop-elevation 8 --terrain-presentation backdrop \
    --sun-elevation 30 --sun-azimuth -55 --debug-view surface --validation \
    "${profile_args[@]}"
}

run_product_png() {
  local output="$1"
  local seed="$2"
  local center="$3"
  local radius="$4"
  local elevation="$5"
  local azimuth="$6"
  local view="$7"
  local width="${8:-1920}"
  local height="${9:-1080}"
  set_center_args "${center}"
  "${PRODUCT_APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-seed "${seed}" "${center_args[@]}" \
    --terrain-camera-preset backdrop-stage --terrain-backdrop-orbit-radius "${radius}" \
    --terrain-backdrop-elevation "${elevation}" --terrain-backdrop-azimuth "${azimuth}" \
    --terrain-presentation backdrop --sun-elevation 30 --sun-azimuth -55 \
    --debug-view "${view}" --validation
}

run_study_png() {
  local output="$1"
  local seed="$2"
  local azimuth="$3"
  "${STUDY_APP}" --headless --frames 1 --width 960 --height 540 \
    --output "${output}" --terrain-recipe mountains-hierarchy-v2 --terrain-seed "${seed}" \
    --directional-lane cached-radial --radial-render-stride 3 \
    --directional-focus-height 500 --directional-orbit-radius 400 \
    --terrain-camera-preset backdrop-stage --terrain-presentation backdrop \
    --terrain-backdrop-azimuth "${azimuth}" --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view surface --validation
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

for seed in "${seeds[@]}"; do
  orbit_dir="${TMP_DIR}/raw/orbits/seed-${seed}"
  video="${orbit_dir}/orbit.mp4"
  mkdir -p "${orbit_dir}"
  run_product_video "${video}" "${seed}" "" 90 1920 1080
  extract_orbit "${video}" "${orbit_dir}"
  rm "${video}"
done

consumer_dir="${TMP_DIR}/raw/centers/consumer-owned"
mkdir -p "${consumer_dir}"
run_product_video "${consumer_dir}/orbit.mp4" 9012 consumer-owned 90 1920 1080
extract_orbit "${consumer_dir}/orbit.mp4" "${consumer_dir}"
rm "${consumer_dir}/orbit.mp4"

seed_inputs=()
for seed in "${seeds[@]}"; do
  for azimuth in "${azimuths[@]}"; do
    seed_inputs+=(
      -label "seed ${seed} / yaw ${azimuth} deg"
      "${TMP_DIR}/raw/orbits/seed-${seed}/azimuth-${azimuth}.png"
    )
  done
done
magick montage "${seed_inputs[@]}" -tile 6x3 -geometry 480x270+8+24 \
  "${TMP_DIR}/radial-v1-seeds-and-yaw.png"

center_inputs=()
for center in continuous consumer-owned; do
  for azimuth in "${azimuths[@]}"; do
    if [[ "${center}" == "continuous" ]]; then
      image="${TMP_DIR}/raw/orbits/seed-9012/azimuth-${azimuth}.png"
    else
      image="${consumer_dir}/azimuth-${azimuth}.png"
    fi
    center_inputs+=( -label "${center} / yaw ${azimuth} deg" "${image}" )
  done
done
magick montage "${center_inputs[@]}" -tile 6x2 -geometry 480x270+8+24 \
  "${TMP_DIR}/radial-v1-center-ownership.png"

parity_inputs=()
parity_pairs=0
for azimuth in "${azimuths[@]}"; do
  product="${TMP_DIR}/raw/parity/product-${azimuth}.png"
  study="${TMP_DIR}/raw/parity/study-${azimuth}.png"
  run_product_png "${product}" 9012 "" 400 8 "${azimuth}" surface 960 540
  run_study_png "${study}" 9012 "${azimuth}"
  product_hash="$(sha256sum "${product}" | awk '{print $1}')"
  study_hash="$(sha256sum "${study}" | awk '{print $1}')"
  if [[ "${product_hash}" != "${study_hash}" ]]; then
    printf 'terrain radial product review: parity mismatch at yaw %s\n' "${azimuth}" >&2
    exit 1
  fi
  parity_pairs="$((parity_pairs + 1))"
  parity_inputs+=( -label "product / yaw ${azimuth} deg" "${product}" )
done
for azimuth in "${azimuths[@]}"; do
  parity_inputs+=(
    -label "study baseline / yaw ${azimuth} deg"
    "${TMP_DIR}/raw/parity/study-${azimuth}.png"
  )
done
magick montage "${parity_inputs[@]}" -tile 6x2 -geometry 480x270+8+24 \
  "${TMP_DIR}/radial-v1-product-study-parity.png"

envelope_inputs=()
for spec in "100 0" "400 8" "1000 30"; do
  read -r radius elevation <<<"${spec}"
  for azimuth in 0 180; do
    output="${TMP_DIR}/raw/envelope/radius-${radius}-elevation-${elevation}-yaw-${azimuth}.png"
    run_product_png "${output}" 9012 "" "${radius}" "${elevation}" "${azimuth}" surface
    envelope_inputs+=(
      -label "${radius} m / ${elevation} deg / yaw ${azimuth} deg"
      "${output}"
    )
  done
done
magick montage "${envelope_inputs[@]}" -tile 2x3 -geometry 720x405+8+24 \
  "${TMP_DIR}/radial-v1-camera-envelope.png"

diagnostic_inputs=()
for view in clay normal projected-edge material-weights stage-ownership; do
  output="${TMP_DIR}/raw/diagnostics/${view}.png"
  run_product_png "${output}" 9012 "" 400 8 0 "${view}"
  diagnostic_inputs+=( -label "${view}" "${output}" )
done
magick montage "${diagnostic_inputs[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${TMP_DIR}/radial-v1-diagnostics.png"

setup_start_ns="$(date +%s%N)"
run_product_png "${TMP_DIR}/setup/first-frame.png" 9012 "" 400 8 0 surface 640 360 &
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

profile_prefix="${TMP_DIR}/profile/radial-v1"
run_product_video "${TMP_DIR}/profile/radial-v1.mp4" 9012 "" 300 2560 1440 \
  "${profile_prefix}" 120 60
rm "${TMP_DIR}/profile/radial-v1.mp4"
durations="${TMP_DIR}/profile/terrain-surface-durations.txt"
awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' \
  "${profile_prefix}.passes.csv" | sort -n > "${durations}"
sample_count="$(wc -l < "${durations}" | tr -d ' ')"
if (( sample_count < 120 )); then
  printf 'terrain radial product review: expected at least 120 GPU samples, found %s\n' \
    "${sample_count}" >&2
  exit 1
fi
p95_rank="$(( (95 * sample_count + 99) / 100 ))"
p95_ms="$(sed -n "${p95_rank}p" "${durations}")"
performance_pass=false
if awk -v value="${p95_ms}" 'BEGIN { exit !(value <= 1.5) }'; then
  performance_pass=true
else
  printf 'terrain radial product review: GPU p95 %s ms exceeds advisory 1.5 ms target\n' \
    "${p95_ms}" >&2
fi

metric_average() {
  local name="$1"
  awk -F, -v name="${name}" \
    '$2 == "terrain.backdrop" && $3 == name { sum += $4; count += 1 } \
     END { if (count == 0) exit 1; printf "%.6f", sum / count }' \
    "${profile_prefix}.metrics.csv"
}

render_stride="$(metric_average render_stride)"
outer_radius_m="$(metric_average outer_radius_m)"
continuous_center="$(metric_average continuous_center)"
render_triangles="$(metric_average product_render_triangles)"
source_samples="$(metric_average source_samples)"

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --argjson parity_pairs "${parity_pairs}" \
  --argjson samples "${sample_count}" \
  --argjson p95_ms "${p95_ms}" \
  --argjson performance_pass "${performance_pass}" \
  --argjson setup_ms "${setup_elapsed_ms}" \
  --argjson setup_rss "${setup_max_rss_kib}" \
  --argjson render_stride "${render_stride}" \
  --argjson outer_radius_m "${outer_radius_m}" \
  --argjson continuous_center "${continuous_center}" \
  --argjson render_triangles "${render_triangles}" \
  --argjson source_samples "${source_samples}" \
  '{
    schema: "cubey.terrain.radial-backdrop-product.v1",
    cubey_commit: $commit,
    product: {
      profile: "radial-v1",
      source: "mountains-hierarchy-v2",
      default_center: "continuous",
      supported_center_modes: ["continuous", "consumer-owned"],
      render_stride: $render_stride,
      outer_radius_m: $outer_radius_m,
      floor_footprint_m: 6000,
      camera_envelope: {orbit_radius_m: [100, 1000], elevation_degrees: [0, 30], yaw_restricted: false}
    },
    parity: {study_lane: "cached-radial", exact_png_pairs: $parity_pairs, passed: true},
    coverage: {seeds: [0, 9012, 12345], yaw_degrees: [0, 60, 120, 180, 240, 300]},
    performance: {
      resolution: [2560, 1440], capture_fps: 120, warmup_frames: 60, samples: $samples,
      terrain_surface_gpu_p95_ms: $p95_ms, advisory_target_ms: 1.5,
      target_met: $performance_pass, blocks_productization: false,
      render_triangle_capacity: $render_triangles, source_samples: $source_samples
    },
    setup_and_first_frame: {resolution: [640, 360], elapsed_ms: $setup_ms, process_max_rss_kib: $setup_rss},
    deferred: ["sub-1-ms optimization", "mid-field detail", "foliage", "hydrology", "streaming"]
  }' > "${TMP_DIR}/review-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Radial Backdrop Product V1

Review in this order:

1. `radial-v1-seeds-and-yaw.png`: check all three seeds across unrestricted yaw
   for convincing far-field massing, transition exposure, and low-poly silhouettes.
2. `radial-v1-camera-envelope.png`: check the supported 100-1000 m orbit and
   0-30 degree elevation envelope at opposing headings.
3. `radial-v1-center-ownership.png`: compare the default continuous floor with
   the consumer-owned center intended for foreground scene integration.
4. `radial-v1-diagnostics.png`: inspect topology, normals, projected triangle
   scale, material continuity, and stage ownership.
5. `radial-v1-product-study-parity.png`: the two rows are exact PNG matches;
   this proves the product path preserved the accepted study result.
6. `review-metadata.json`: records setup cost and the advisory 1440p terrain
   surface GPU p95 target. The profile runs at 120 fps to improve GPU clock
   residency, but pass timing still varies with device power state. Performance
   does not block this productization batch; the sub-1-ms target remains follow-up
   work.

This pack validates a far-field backdrop product. It does not claim mid-field or
surface-scene fidelity.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT

printf 'terrain radial product review: wrote %s\n' "${OUT_DIR}"
printf '  exact study parity: %s/%s pairs\n' "${parity_pairs}" "${#azimuths[@]}"
printf '  terrain surface GPU p95: %s ms (advisory target 1.5 ms, met: %s)\n' \
  "${p95_ms}" "${performance_pass}"
printf '  setup and first frame: %s ms, %s KiB max RSS\n' \
  "${setup_elapsed_ms}" "${setup_max_rss_kib}"
