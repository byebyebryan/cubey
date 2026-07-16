#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_study}"
REPORT="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_report}"
OUT_DIR="${3:-${ROOT_DIR}/outputs/terrain/directional-backdrop-study-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${APP}" "${REPORT}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain directional backdrop study: executable not found: %s\n' \
      "${executable}" >&2
    exit 2
  fi
done
for command in ffmpeg jq magick sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain directional backdrop study: %s is required\n' "${command}" >&2
    exit 2
  fi
done

hierarchy_recipe="mountains-hierarchy-v2"
control_recipe="control-v2-1"
seeds=(0 9012 12345)
lanes=(hard-cut continuous-current placement shaped)
control_lanes=(placement shaped)
azimuths=(0 60 120 180 240 300)
frames=(0 15 30 45 60 75)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/orbits" \
  "${TMP_DIR}/raw/envelope" "${TMP_DIR}/raw/presentation" "${TMP_DIR}/profile"

common_args=(
  --terrain-camera-preset backdrop-stage
  --terrain-presentation backdrop
  --terrain-backdrop-orbit-radius 100
  --terrain-backdrop-elevation 8
  --sun-elevation 30
  --sun-azimuth -55
)

for seed in "${seeds[@]}"; do
  "${REPORT}" --output-dir "${TMP_DIR}/reports/${hierarchy_recipe}/seed-${seed}" \
    --recipe "${hierarchy_recipe}" --seed "${seed}" --grid-size 512
done
"${REPORT}" --output-dir "${TMP_DIR}/reports/${control_recipe}/seed-9012" \
  --recipe "${control_recipe}" --seed 9012 --grid-size 512

capture_orbit() {
  local recipe="$1"
  local seed="$2"
  local lane="$3"
  local lane_dir="${TMP_DIR}/raw/orbits/${recipe}/seed-${seed}/${lane}"
  local video="${lane_dir}/orbit.mp4"
  mkdir -p "${lane_dir}"

  "${APP}" --headless --capture video --frames 90 --fps 30 \
    --width 1920 --height 1080 --output "${video}" \
    --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane "${lane}" --debug-view clay "${common_args[@]}"

  for index in "${!frames[@]}"; do
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${frames[index]})" -frames:v 1 \
      "${lane_dir}/azimuth-${azimuths[index]}.png"
  done
  rm "${video}"
}

for seed in "${seeds[@]}"; do
  for lane in "${lanes[@]}"; do
    capture_orbit "${hierarchy_recipe}" "${seed}" "${lane}"
  done
done
for lane in "${control_lanes[@]}"; do
  capture_orbit "${control_recipe}" 9012 "${lane}"
done

for seed in "${seeds[@]}"; do
  inputs=()
  for lane in "${lanes[@]}"; do
    for azimuth in "${azimuths[@]}"; do
      inputs+=(
        -label "${lane} / ${azimuth} deg"
        "${TMP_DIR}/raw/orbits/${hierarchy_recipe}/seed-${seed}/${lane}/azimuth-${azimuth}.png"
      )
    done
  done
  magick montage "${inputs[@]}" -tile 6x4 -geometry 480x270+8+24 \
    "${TMP_DIR}/directional-backdrop-clay-seed-${seed}.png"
done

control_inputs=()
for lane in "${control_lanes[@]}"; do
  for azimuth in "${azimuths[@]}"; do
    control_inputs+=(
      -label "${lane} / ${azimuth} deg"
      "${TMP_DIR}/raw/orbits/${control_recipe}/seed-9012/${lane}/azimuth-${azimuth}.png"
    )
  done
done
magick montage "${control_inputs[@]}" -tile 6x2 -geometry 480x270+8+24 \
  "${TMP_DIR}/directional-backdrop-v2-1-control.png"

for seed in "${seeds[@]}"; do
  report_dir="${TMP_DIR}/reports/${hierarchy_recipe}/seed-${seed}"
  diagnostic_inputs=()
  for field in base-height base-slope placement-map floor-height broad-gate detail-gate \
    shaped-height shaped-slope; do
    diagnostic_inputs+=(
      -label "${field}"
      "${report_dir}/${field}.png"
    )
  done
  magick montage "${diagnostic_inputs[@]}" -tile 4x2 -geometry 384x384+8+24 \
    "${TMP_DIR}/directional-backdrop-diagnostics-seed-${seed}.png"
done

capture_envelope() {
  local radius="$1"
  local elevation="$2"
  local facing="$3"
  local extra_args=()
  local report="${TMP_DIR}/reports/${hierarchy_recipe}/seed-9012/report.json"
  if [[ "${facing}" == "open" ]]; then
    local opposite_degrees
    opposite_degrees="$(jq -r \
      '(.placement.mountain_yaw_radians * 180 / 3.141592653589793 + 180) % 360' \
      "${report}")"
    extra_args=(--terrain-backdrop-azimuth "${opposite_degrees}")
  fi
  local output="${TMP_DIR}/raw/envelope/radius-${radius}-elevation-${elevation}-${facing}.png"
  "${APP}" --headless --frames 1 --width 1920 --height 1080 --output "${output}" \
    --terrain-recipe "${hierarchy_recipe}" --terrain-seed 9012 \
    --directional-lane shaped --terrain-camera-preset backdrop-stage \
    --terrain-presentation backdrop --terrain-backdrop-orbit-radius "${radius}" \
    --terrain-backdrop-elevation "${elevation}" --sun-elevation 30 --sun-azimuth -55 \
    --debug-view clay "${extra_args[@]}"
}

envelope_inputs=()
for spec in "50 0" "100 8" "250 30"; do
  read -r radius elevation <<<"${spec}"
  for facing in mountain open; do
    capture_envelope "${radius}" "${elevation}" "${facing}"
    envelope_inputs+=(
      -label "${radius} m / ${elevation} deg / ${facing}"
      "${TMP_DIR}/raw/envelope/radius-${radius}-elevation-${elevation}-${facing}.png"
    )
  done
done
magick montage "${envelope_inputs[@]}" -tile 2x3 -geometry 640x360+8+24 \
  "${TMP_DIR}/directional-backdrop-orbit-envelope.png"

presentation_inputs=()
for seed in "${seeds[@]}"; do
  output="${TMP_DIR}/raw/presentation/seed-${seed}.png"
  "${APP}" --headless --frames 1 --width 2560 --height 1440 --output "${output}" \
    --terrain-recipe "${hierarchy_recipe}" --terrain-seed "${seed}" \
    --directional-lane shaped --debug-view surface "${common_args[@]}"
  presentation_inputs+=( -label "shaped / seed ${seed}" "${output}" )
done
magick montage "${presentation_inputs[@]}" -tile 3x1 -geometry 800x450+8+24 \
  "${TMP_DIR}/directional-backdrop-presentation.png"

profile_lane() {
  local lane="$1"
  local prefix="${TMP_DIR}/profile/${lane}-seed-9012"
  local video="${prefix}.mp4"
  local durations="${prefix}-terrain-surface-durations.txt"
  "${APP}" --headless --capture video --frames 180 --fps 30 \
    --width 2560 --height 1440 --output "${video}" \
    --terrain-recipe "${hierarchy_recipe}" --terrain-seed 9012 \
    --directional-lane "${lane}" --debug-view surface "${common_args[@]}" \
    --profile-output "${prefix}" --profile-warmup-frames 30
  rm "${video}"

  awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' \
    "${prefix}.passes.csv" | sort -n > "${durations}"
  local sample_count
  sample_count="$(wc -l < "${durations}" | tr -d ' ')"
  if (( sample_count < 120 )); then
    printf 'terrain directional backdrop study: expected 120 GPU samples for %s, found %s\n' \
      "${lane}" "${sample_count}" >&2
    exit 1
  fi
  local p95_rank="$(( (95 * sample_count + 99) / 100 ))"
  local p95_ms
  p95_ms="$(sed -n "${p95_rank}p" "${durations}")"
  printf '%s %s\n' "${sample_count}" "${p95_ms}" > "${prefix}-result.txt"
}

profile_lane hard-cut
profile_lane shaped
read -r hard_cut_sample_count hard_cut_p95_ms \
  < "${TMP_DIR}/profile/hard-cut-seed-9012-result.txt"
read -r shaped_sample_count shaped_p95_ms \
  < "${TMP_DIR}/profile/shaped-seed-9012-result.txt"

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson lanes "$(printf '%s\n' "${lanes[@]}" | jq -R . | jq -s .)" \
  --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson hard_cut_sample_count "${hard_cut_sample_count}" \
  --argjson hard_cut_p95_ms "${hard_cut_p95_ms}" \
  --argjson shaped_sample_count "${shaped_sample_count}" \
  --argjson shaped_p95_ms "${shaped_p95_ms}" \
  '{
    schema: "cubey.terrain.directional-backdrop-capture.v1",
    cubey_commit: $commit,
    source: {recipe: "mountains-hierarchy-v2", seeds: $seeds},
    lanes: $lanes,
    camera: {
      unrestricted_yaw: true,
      azimuth_degrees: $azimuths,
      standard_orbit: {radius_m: 100, elevation_degrees: 8},
      envelope: [
        {radius_m: 50, elevation_degrees: 0},
        {radius_m: 100, elevation_degrees: 8},
        {radius_m: 250, elevation_degrees: 30}
      ]
    },
    render: {resolution: [1920, 1080], mesh_density: "high", render_stride: 1},
    performance: {
      resolution: [2560, 1440],
      warmup_frames: 30,
      terrain_surface_gpu_p95_limit_ms: 1.0,
      lanes: {
        "hard-cut": {
          terrain_surface_gpu_sample_count: $hard_cut_sample_count,
          terrain_surface_gpu_p95_ms: $hard_cut_p95_ms,
          pass: ($hard_cut_p95_ms < 1.0)
        },
        shaped: {
          terrain_surface_gpu_sample_count: $shaped_sample_count,
          terrain_surface_gpu_p95_ms: $shaped_p95_ms,
          pass: ($shaped_p95_ms < 1.0)
        }
      }
    }
  }' > "${TMP_DIR}/capture-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Directional Backdrop Study v1

Review in this order:

1. `directional-backdrop-diagnostics-seed-*.png`: the placement map marks
   measured mountain sectors red, open sectors blue, and the selected mountain
   direction yellow. The gate images must not expose a straight uplift front.
2. `directional-backdrop-clay-seed-*.png`: rows are the hard cut, continuous
   current focus, placement-only focus, and shaped terrain. Read across all six
   yaw angles; no favorable heading is sufficient by itself.
3. `directional-backdrop-v2-1-control.png`: checks whether the composition
   depends specifically on hierarchy-v2 morphology.
4. `directional-backdrop-orbit-envelope.png`: checks center continuity,
   camera/terrain intersection, and whether the low side remains usable from
   the fixed 50/100/250 m orbit envelope.
5. `directional-backdrop-presentation.png`: final material and atmosphere are
   compatibility evidence only. Source silhouette and gate artifacts should be
   judged in clay and diagnostics first.

The hard-cut row is the accepted cached product before this study. The study
does not promote either directional lane into production.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'terrain directional backdrop study: wrote %s (hard cut %s ms; shaped %s ms)\n' \
  "${OUT_DIR}" "${hard_cut_p95_ms}" "${shaped_p95_ms}"
