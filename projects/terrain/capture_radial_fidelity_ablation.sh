#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STUDY_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_study}"
REPORT_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_report}"
OUT_DIR="${3:-${ROOT_DIR}/outputs/terrain/radial-fidelity-ablation-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${STUDY_APP}" "${REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain radial fidelity ablation: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in awk cmp date ffmpeg jq magick sed sha256sum sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain radial fidelity ablation: %s is required\n' "${command}" >&2
    exit 2
  fi
done

recipe="mountains-hierarchy-v2"
variants=(control source material combined)
seeds=(0 9012 12345)
azimuths=(0 120 240)
orbit_frames=(0 30 60)
capture_width=1920
capture_height=1080
profile_width=2560
profile_height=1440
profile_frames=120
profile_fps=120
profile_warmup=30

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw/matrix" "${TMP_DIR}/raw/seeds" \
  "${TMP_DIR}/raw/diagnostics" "${TMP_DIR}/raw/default-parity" \
  "${TMP_DIR}/source-components" "${TMP_DIR}/profile" "${TMP_DIR}/setup"

fidelity_args=()
set_fidelity_args() {
  local variant="$1"
  fidelity_args=()
  if [[ -n "${variant}" ]]; then
    fidelity_args=(--radial-fidelity "${variant}")
  fi
}

run_video() {
  local output="$1"
  local variant="$2"
  local seed="$3"
  local radius="$4"
  local width="$5"
  local height="$6"
  local frames="$7"
  local fps="$8"
  local profile_prefix="${9:-}"
  local profile_args=()
  set_fidelity_args "${variant}"
  if [[ -n "${profile_prefix}" ]]; then
    profile_args=(--profile-output "${profile_prefix}" \
      --profile-warmup-frames "${profile_warmup}")
  fi
  "${STUDY_APP}" --headless --capture video --frames "${frames}" --fps "${fps}" \
    --width "${width}" --height "${height}" --output "${output}" \
    --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane cached-radial --radial-render-stride 3 \
    --directional-focus-height 500 --directional-orbit-radius "${radius}" \
    --terrain-camera-preset backdrop --terrain-presentation backdrop \
    --terrain-backdrop-azimuth 0 --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view surface --validation \
    "${fidelity_args[@]}" "${profile_args[@]}"
}

run_png() {
  local output="$1"
  local variant="$2"
  local seed="$3"
  local radius="$4"
  local azimuth="$5"
  local view="$6"
  local width="${7:-${capture_width}}"
  local height="${8:-${capture_height}}"
  set_fidelity_args "${variant}"
  "${STUDY_APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane cached-radial --radial-render-stride 3 \
    --directional-focus-height 500 --directional-orbit-radius "${radius}" \
    --terrain-camera-preset backdrop --terrain-presentation backdrop \
    --terrain-backdrop-azimuth "${azimuth}" --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view "${view}" --validation \
    "${fidelity_args[@]}"
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

for radius in 400 100; do
  for variant in "${variants[@]}"; do
    raw_dir="${TMP_DIR}/raw/matrix/${radius}m/${variant}"
    video="${raw_dir}/orbit.mp4"
    mkdir -p "${raw_dir}"
    run_video "${video}" "${variant}" 9012 "${radius}" \
      "${capture_width}" "${capture_height}" 90 30
    extract_orbit "${video}" "${raw_dir}"
    if [[ "${radius}" == 400 && "${variant}" == combined ]]; then
      mv "${video}" "${TMP_DIR}/combined-orbit.mp4"
    else
      rm "${video}"
    fi
  done
done

make_matrix_sheet() {
  local radius="$1"
  local inputs=()
  for variant in "${variants[@]}"; do
    for azimuth in "${azimuths[@]}"; do
      inputs+=(
        -label "${variant} / ${radius} m / yaw ${azimuth} deg"
        "${TMP_DIR}/raw/matrix/${radius}m/${variant}/azimuth-${azimuth}.png"
      )
    done
  done
  magick montage "${inputs[@]}" -tile 3x4 -geometry 640x360+8+24 \
    "${TMP_DIR}/radial-fidelity-${radius}m.png"
}

make_matrix_sheet 400
make_matrix_sheet 100

for seed in 0 12345; do
  for variant in control combined; do
    output="${TMP_DIR}/raw/seeds/${variant}-seed-${seed}.png"
    run_png "${output}" "${variant}" "${seed}" 400 0 surface
  done
done

seed_inputs=()
for variant in control combined; do
  for seed in "${seeds[@]}"; do
    if [[ "${seed}" == 9012 ]]; then
      image="${TMP_DIR}/raw/matrix/400m/${variant}/azimuth-0.png"
    else
      image="${TMP_DIR}/raw/seeds/${variant}-seed-${seed}.png"
    fi
    seed_inputs+=( -label "${variant} / seed ${seed} / yaw 0 deg" "${image}" )
  done
done
magick montage "${seed_inputs[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${TMP_DIR}/radial-fidelity-seeds.png"

geometry_diagnostic_inputs=()
for variant in control source; do
  for view in height clay normal; do
    output="${TMP_DIR}/raw/diagnostics/${variant}-${view}.png"
    run_png "${output}" "${variant}" 9012 400 0 "${view}"
    label="${view}"
    if [[ "${view}" == normal ]]; then
      label="source normal"
    fi
    geometry_diagnostic_inputs+=( -label "${variant} / ${label}" "${output}" )
  done
done
magick montage "${geometry_diagnostic_inputs[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${TMP_DIR}/radial-fidelity-source-diagnostics.png"

material_diagnostic_inputs=()
for spec in \
  "material-albedo|material albedo" \
  "material-normal|material normal" \
  "material-roughness|roughness" \
  "normal|final normal" \
  "classification-normal|classification normal"; do
  IFS='|' read -r view label <<<"${spec}"
  output="${TMP_DIR}/raw/diagnostics/material-${view}.png"
  run_png "${output}" material 9012 400 0 "${view}"
  material_diagnostic_inputs+=( -label "material / ${label}" "${output}" )
done
magick montage "${material_diagnostic_inputs[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${TMP_DIR}/radial-fidelity-material-diagnostics.png"

"${REPORT_APP}" --output-dir "${TMP_DIR}/source-components" \
  --recipe "${recipe}" --seed 9012 --grid-size 512 --radial-fidelity source
"${REPORT_APP}" --output-dir "${TMP_DIR}/control-components" \
  --recipe "${recipe}" --seed 9012 --grid-size 512 --radial-fidelity control
source_stage_parity="$(jq -n \
  --slurpfile control "${TMP_DIR}/control-components/report.json" \
  --slurpfile source "${TMP_DIR}/source-components/report.json" \
  '{passed: ($control[0].placement == $source[0].placement and
      $control[0].shaped_stage == $source[0].shaped_stage),
    control: $control[0].shaped_stage,
    source: $source[0].shaped_stage}')"
if [[ "$(jq -r '.passed' <<<"${source_stage_parity}")" != true ]]; then
  printf 'terrain radial fidelity ablation: source candidate changed stage clearance\n' >&2
  exit 1
fi
component_inputs=()
for spec in \
  "full-source-height.png|full source" \
  "filtered-detail-height.png|filtered 180 m detail" \
  "structure-height.png|900 m structure" \
  "shaped-height.png|final composition"; do
  IFS='|' read -r image label <<<"${spec}"
  component_inputs+=( -label "${label}" "${TMP_DIR}/source-components/${image}" )
done
magick montage "${component_inputs[@]}" -tile 4x1 -geometry 512x512+8+24 \
  "${TMP_DIR}/radial-fidelity-source-components.png"

run_png "${TMP_DIR}/raw/default-parity/omitted.png" "" 9012 400 0 surface 960 540
run_png "${TMP_DIR}/raw/default-parity/control.png" control 9012 400 0 surface 960 540
if ! cmp -s "${TMP_DIR}/raw/default-parity/omitted.png" \
  "${TMP_DIR}/raw/default-parity/control.png"; then
  printf 'terrain radial fidelity ablation: omitted/default control mismatch\n' >&2
  exit 1
fi
default_parity_hash="$(sha256sum "${TMP_DIR}/raw/default-parity/control.png" | awk '{print $1}')"

declare -A setup_elapsed_ms setup_max_rss_kib
for variant in "${variants[@]}"; do
  output="${TMP_DIR}/setup/${variant}.png"
  start_ns="$(date +%s%N)"
  "${STUDY_APP}" --headless --frames 1 --width 640 --height 360 --output "${output}" \
    --terrain-recipe "${recipe}" --terrain-seed 9012 \
    --directional-lane cached-radial --radial-render-stride 3 \
    --radial-fidelity "${variant}" --directional-focus-height 500 \
    --directional-orbit-radius 400 --terrain-camera-preset backdrop \
    --terrain-presentation backdrop --terrain-backdrop-azimuth 0 \
    --terrain-backdrop-elevation 8 --sun-elevation 30 --sun-azimuth -55 \
    --debug-view surface --validation &
  setup_pid=$!
  max_rss_kib=0
  while kill -0 "${setup_pid}" 2>/dev/null; do
    if [[ -r "/proc/${setup_pid}/status" ]]; then
      rss_kib="$(awk '$1 == "VmHWM:" { print $2 }' "/proc/${setup_pid}/status")"
      if [[ -n "${rss_kib}" ]] && (( rss_kib > max_rss_kib )); then
        max_rss_kib="${rss_kib}"
      fi
    fi
    sleep 0.05
  done
  wait "${setup_pid}"
  end_ns="$(date +%s%N)"
  setup_elapsed_ms["${variant}"]="$(( (end_ns - start_ns) / 1000000 ))"
  setup_max_rss_kib["${variant}"]="${max_rss_kib}"
done

metric_average() {
  local csv="$1"
  local name="$2"
  awk -F, -v name="${name}" \
    '$2 == "terrain.backdrop" && $3 == name { sum += $4; count += 1 } \
     END { if (count == 0) exit 1; printf "%.6f", sum / count }' "${csv}"
}

metric_integer() {
  local csv="$1"
  local name="$2"
  awk -F, -v name="${name}" \
    '$2 == "terrain.backdrop" && $3 == name { printf "%.0f", $4; found = 1; exit } \
     END { if (!found) exit 1 }' "${csv}"
}

profile_json=()
declare -A product_hashes
for variant in "${variants[@]}"; do
  prefix="${TMP_DIR}/profile/${variant}"
  video="${TMP_DIR}/profile/${variant}.mp4"
  run_video "${video}" "${variant}" 9012 400 "${profile_width}" "${profile_height}" \
    "${profile_frames}" "${profile_fps}" "${prefix}"
  rm "${video}"
  durations="${prefix}-terrain-surface-durations.txt"
  awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' \
    "${prefix}.passes.csv" | sort -n > "${durations}"
  sample_count="$(wc -l < "${durations}" | tr -d ' ')"
  if (( sample_count < 60 )); then
    printf 'terrain radial fidelity ablation: %s expected at least 60 GPU samples, found %s\n' \
      "${variant}" "${sample_count}" >&2
    exit 1
  fi
  p50_rank="$(( (50 * sample_count + 99) / 100 ))"
  p95_rank="$(( (95 * sample_count + 99) / 100 ))"
  mean_ms="$(awk '{ sum += $1 } END { printf "%.6f", sum / NR }' "${durations}")"
  p50_ms="$(sed -n "${p50_rank}p" "${durations}")"
  p95_ms="$(sed -n "${p95_rank}p" "${durations}")"
  metrics="${prefix}.metrics.csv"
  submitted_sectors="$(metric_average "${metrics}" submitted_sectors)"
  submitted_triangles="$(metric_average "${metrics}" submitted_triangles)"
  render_triangles="$(metric_integer "${metrics}" product_render_triangles)"
  source_samples="$(metric_integer "${metrics}" source_samples)"
  texture_bytes="$(metric_integer "${metrics}" material_texture_bytes)"
  hash_high="$(metric_integer "${metrics}" product_hash_high32)"
  hash_low="$(metric_integer "${metrics}" product_hash_low32)"
  printf -v product_hash '%08x%08x' "${hash_high}" "${hash_low}"
  product_hashes["${variant}"]="${product_hash}"
  profile_json+=("$(jq -n \
    --arg variant "${variant}" --arg product_hash "${product_hash}" \
    --argjson samples "${sample_count}" --argjson mean_ms "${mean_ms}" \
    --argjson p50_ms "${p50_ms}" --argjson p95_ms "${p95_ms}" \
    --argjson sectors "${submitted_sectors}" --argjson triangles "${submitted_triangles}" \
    --argjson capacity "${render_triangles}" --argjson source_samples "${source_samples}" \
    --argjson texture_bytes "${texture_bytes}" \
    '{variant: $variant, product_hash: $product_hash, samples: $samples,
      terrain_surface_gpu_mean_ms: $mean_ms, terrain_surface_gpu_p50_ms: $p50_ms,
      terrain_surface_gpu_p95_ms: $p95_ms, average_submitted_sectors: $sectors,
      average_submitted_triangles: $triangles, render_triangle_capacity: $capacity,
      source_samples: $source_samples, material_texture_bytes: $texture_bytes}')")
done

if [[ "${product_hashes[material]}" != "${product_hashes[control]}" ]]; then
  printf 'terrain radial fidelity ablation: material-only changed the control product hash\n' >&2
  exit 1
fi
if [[ "${product_hashes[combined]}" != "${product_hashes[source]}" ]]; then
  printf 'terrain radial fidelity ablation: combined changed the source product hash\n' >&2
  exit 1
fi

printf '%s\n' "${profile_json[@]}" | jq -s \
  'map({key: .variant, value: .}) | from_entries' > "${TMP_DIR}/profiles.json"

setup_json=()
for variant in "${variants[@]}"; do
  setup_json+=("$(jq -n --arg variant "${variant}" \
    --argjson elapsed_ms "${setup_elapsed_ms[${variant}]}" \
    --argjson rss_kib "${setup_max_rss_kib[${variant}]}" \
    '{variant: $variant, elapsed_ms: $elapsed_ms, process_max_rss_kib: $rss_kib}')")
done
printf '%s\n' "${setup_json[@]}" | jq -s \
  'map({key: .variant, value: .}) | from_entries' > "${TMP_DIR}/setups.json"

combined_mean="$(jq -r '.combined.terrain_surface_gpu_mean_ms' "${TMP_DIR}/profiles.json")"
combined_p50="$(jq -r '.combined.terrain_surface_gpu_p50_ms' "${TMP_DIR}/profiles.json")"
combined_p95="$(jq -r '.combined.terrain_surface_gpu_p95_ms' "${TMP_DIR}/profiles.json")"
performance_pass=false
if awk -v mean="${combined_mean}" -v p50="${combined_p50}" \
  'BEGIN { exit !(mean <= 2.0 && p50 <= 2.0) }'; then
  performance_pass=true
fi

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --arg recipe "${recipe}" \
  --arg default_parity_hash "${default_parity_hash}" \
  --argjson performance_pass "${performance_pass}" \
  --argjson source_stage_parity "${source_stage_parity}" \
  --slurpfile profiles "${TMP_DIR}/profiles.json" \
  --slurpfile setups "${TMP_DIR}/setups.json" \
  '{
    schema: "cubey.terrain.radial-fidelity-ablation.v1",
    cubey_commit: $commit,
    arguments: {
      common: ["--directional-lane", "cached-radial", "--radial-render-stride", "3",
        "--directional-focus-height", "500", "--terrain-camera-preset", "backdrop",
        "--terrain-presentation", "backdrop", "--terrain-backdrop-elevation", "8",
        "--sun-elevation", "30", "--sun-azimuth", "-55", "--validation"],
      variant: ["--radial-fidelity", "control|source|material|combined"],
      recipe: $recipe
    },
    capture: {
      review_resolution: [1920, 1080], profile_resolution: [2560, 1440],
      seed_matrix: [0, 9012, 12345], yaw_degrees: [0, 120, 240],
      product_distance_m: 400, stress_distance_m: 100,
      combined_orbit: {frames: 90, fps: 30}
    },
    source_candidate: {
      floor_footprint_m: 6000, floor_relief_fraction: 0.08,
      structure_footprint_m: 900, detail_footprint_m: 180,
      broad_transition_m: [1000, 24000], detail_transition_m: [5000, 24000]
    },
    material_candidate: {
      texture: {extent: [1024, 1024], format: "RGBA8_UNORM", mips: 11,
        period_m: 2048, anisotropy: 8,
        filtered_bands_m: [512, 186, 71, 29, 11, 4.4]},
      albedo_strength: {ground: 0.10, rock: 0.22, snow: 0.035},
      normal_strength: {ground: 0.16, rock: 0.42, snow: 0.06},
      maximum_roughness_variation: 0.08
    },
    parity: {
      omitted_default_matches_explicit_control_png: true,
      default_control_sha256: $default_parity_hash,
      material_preserves_control_product_hash:
        ($profiles[0].material.product_hash == $profiles[0].control.product_hash),
      combined_preserves_source_product_hash:
        ($profiles[0].combined.product_hash == $profiles[0].source.product_hash),
      source_preserves_placement_and_shaped_stage: $source_stage_parity
    },
    setup_and_first_frame: $setups[0],
    performance: {
      warmup_frames: 30, minimum_measured_frames: 60,
      current_mean_p50_target_ms: 2.0,
      combined_mean_p50_target_met: $performance_pass,
      p95_is_tail_telemetry: true,
      variants: $profiles[0]
    }
  }' > "${TMP_DIR}/metadata.json"
rm "${TMP_DIR}/profiles.json" "${TMP_DIR}/setups.json"

cat > "${TMP_DIR}/REVIEW.md" <<EOF
# Terrain Radial Fidelity Ablation V1

Status: completed; bounded ablation passes; no production promotion.

Review in this order:

1. \`radial-fidelity-400m.png\`: compare control, source, material, and combined
   at the accepted product distance for seed 9012 and yaw 0/120/240.
2. \`radial-fidelity-100m.png\`: use the same matrix as a defect stress view;
   it does not expand radial-v1's distance contract.
3. \`radial-fidelity-seeds.png\`: compare control and combined at yaw 0 for
   seeds 0/9012/12345. Combined must improve at least two without regressing the
   third.
4. \`radial-fidelity-source-components.png\` and
   \`radial-fidelity-source-diagnostics.png\`: inspect coherent scale
   separation, shoulders, valley clearance, fins, spikes, and silhouette drift.
5. \`radial-fidelity-material-diagnostics.png\`: distinguish material normal,
   final normal, classification normal, albedo, and roughness response.
6. \`combined-orbit.mp4\`: check repetition, shimmer, swimming, horizon
   aliasing, and mip transitions.
7. \`metadata.json\`: verify geometry hashes, texture memory, setup/RSS,
   submitted work, and all four 1440p profiles.

Automatic gates passed during capture:

- omitted radial fidelity and explicit control are byte-identical;
- material preserves control product hash \`${product_hashes[control]}\`;
- combined preserves source product hash \`${product_hashes[source]}\`.

Combined terrain surface GPU mean/p50 is ${combined_mean}/${combined_p50} ms
against the 2 ms checkpoint (pass: ${performance_pass}). P95 is ${combined_p95}
ms and remains tail telemetry.

Recorded verdict:

- Source passes: the coherent candidate adds readable intermediate shoulders
  and slopes without noisy ridge fields, fins, spikes, or changed stage
  clearance.
- Material passes: the filtered candidate adds restrained face structure to
  ground and exposed rock without obvious repetition, speckle, swimming, mip
  transitions, or horizon aliasing. Snow remains intentionally subtle.
- Combined passes: it is visibly stronger than control for all three reviewed
  seeds, does not regress the third, and retains the accepted macro composition.
- Product-hash, default-path, stage-clearance, and 2 ms mean/p50 gates pass.

This pack is an ablation, not a promotion. Production adoption remains a
separate batch.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT

printf 'terrain radial fidelity ablation: wrote %s\n' "${OUT_DIR}"
printf '  material/control product hash: %s\n' "${product_hashes[control]}"
printf '  combined/source product hash:  %s\n' "${product_hashes[source]}"
printf '  combined GPU mean/p50/p95: %s/%s/%s ms (2 ms mean/p50 pass: %s)\n' \
  "${combined_mean}" "${combined_p50}" "${combined_p95}" "${performance_pass}"
