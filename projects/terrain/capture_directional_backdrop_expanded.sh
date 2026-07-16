#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_study}"
REPORT="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_report}"
OUT_DIR="${3:-${ROOT_DIR}/outputs/terrain/directional-backdrop-expanded-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${APP}" "${REPORT}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain expanded backdrop study: executable not found: %s\n' \
      "${executable}" >&2
    exit 2
  fi
done
for command in ffmpeg jq magick sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain expanded backdrop study: %s is required\n' "${command}" >&2
    exit 2
  fi
done

hierarchy_recipe="mountains-hierarchy-v2"
control_recipe="control-v2-1"
seeds=(0 9012 12345)
focus_heights=(100 500 1000)
orbit_radii=(100 400 1000)
azimuths=(0 60 120 180 240 300)
frames=(0 15 30 45 60 75)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/orbits" "${TMP_DIR}/profile"

for seed in "${seeds[@]}"; do
  "${REPORT}" --output-dir "${TMP_DIR}/reports/${hierarchy_recipe}/seed-${seed}" \
    --recipe "${hierarchy_recipe}" --seed "${seed}" --grid-size 1024 \
    --expanded --focus-height 500
done
"${REPORT}" --output-dir "${TMP_DIR}/reports/${control_recipe}/seed-9012" \
  --recipe "${control_recipe}" --seed 9012 --grid-size 1024 \
  --expanded --focus-height 500

capture_orbit() {
  local recipe="$1"
  local seed="$2"
  local focus_height="$3"
  local radius="$4"
  local view="$5"
  local lane_dir="${TMP_DIR}/raw/orbits/${recipe}/seed-${seed}/focus-${focus_height}/radius-${radius}/${view}"
  local video="${lane_dir}/orbit.mp4"
  if [[ -f "${lane_dir}/azimuth-300.png" ]]; then
    return
  fi
  mkdir -p "${lane_dir}"
  "${APP}" --headless --capture video --frames 90 --fps 30 \
    --width 1920 --height 1080 --output "${video}" \
    --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane expanded-shaped --directional-focus-height "${focus_height}" \
    --directional-orbit-radius "${radius}" \
    --terrain-camera-preset backdrop-stage --terrain-presentation backdrop \
    --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view "${view}"
  for index in "${!frames[@]}"; do
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${frames[index]})" -frames:v 1 \
      "${lane_dir}/azimuth-${azimuths[index]}.png"
  done
  rm "${video}"
}

for seed in "${seeds[@]}"; do
  capture_orbit "${hierarchy_recipe}" "${seed}" 500 400 surface
done
for focus_height in "${focus_heights[@]}"; do
  capture_orbit "${hierarchy_recipe}" 9012 "${focus_height}" 400 surface
done
for radius in "${orbit_radii[@]}"; do
  capture_orbit "${hierarchy_recipe}" 9012 500 "${radius}" surface
done
capture_orbit "${hierarchy_recipe}" 9012 500 400 clay
capture_orbit "${control_recipe}" 9012 500 400 surface

make_orbit_sheet() {
  local output="$1"
  shift
  local specs=("$@")
  local inputs=()
  for spec in "${specs[@]}"; do
    read -r label recipe seed focus_height radius view <<<"${spec}"
    for azimuth in "${azimuths[@]}"; do
      inputs+=(
        -label "${label} / ${azimuth} deg"
        "${TMP_DIR}/raw/orbits/${recipe}/seed-${seed}/focus-${focus_height}/radius-${radius}/${view}/azimuth-${azimuth}.png"
      )
    done
  done
  magick montage "${inputs[@]}" -tile 6x"${#specs[@]}" -geometry 480x270+8+24 \
    "${output}"
}

make_orbit_sheet "${TMP_DIR}/expanded-surface-seeds.png" \
  "seed-0 ${hierarchy_recipe} 0 500 400 surface" \
  "seed-9012 ${hierarchy_recipe} 9012 500 400 surface" \
  "seed-12345 ${hierarchy_recipe} 12345 500 400 surface"
make_orbit_sheet "${TMP_DIR}/expanded-surface-focus-height.png" \
  "focus-100m ${hierarchy_recipe} 9012 100 400 surface" \
  "focus-500m ${hierarchy_recipe} 9012 500 400 surface" \
  "focus-1000m ${hierarchy_recipe} 9012 1000 400 surface"
make_orbit_sheet "${TMP_DIR}/expanded-surface-orbit-radius.png" \
  "radius-100m ${hierarchy_recipe} 9012 500 100 surface" \
  "radius-400m ${hierarchy_recipe} 9012 500 400 surface" \
  "radius-1000m ${hierarchy_recipe} 9012 500 1000 surface"
make_orbit_sheet "${TMP_DIR}/expanded-clay-control.png" \
  "clay ${hierarchy_recipe} 9012 500 400 clay"
make_orbit_sheet "${TMP_DIR}/expanded-v2-1-surface-control.png" \
  "v2.1 ${control_recipe} 9012 500 400 surface"

for recipe_seed in \
  "${hierarchy_recipe} 0" \
  "${hierarchy_recipe} 9012" \
  "${hierarchy_recipe} 12345" \
  "${control_recipe} 9012"; do
  read -r recipe seed <<<"${recipe_seed}"
  report_dir="${TMP_DIR}/reports/${recipe}/seed-${seed}"
  diagnostic_inputs=()
  for field in base-height floor-height shaped-height base-slope shaped-slope \
    broad-gate detail-gate placement-map; do
    diagnostic_inputs+=( -label "${field}" "${report_dir}/${field}.png" )
  done
  magick montage "${diagnostic_inputs[@]}" -tile 4x2 -geometry 384x384+8+24 \
    "${TMP_DIR}/expanded-diagnostics-${recipe}-seed-${seed}.png"
done

profile_prefix="${TMP_DIR}/profile/expanded-seed-9012"
profile_video="${profile_prefix}.mp4"
"${APP}" --headless --capture video --frames 180 --fps 30 \
  --width 2560 --height 1440 --output "${profile_video}" \
  --terrain-recipe "${hierarchy_recipe}" --terrain-seed 9012 \
  --directional-lane expanded-shaped --directional-focus-height 500 \
  --directional-orbit-radius 400 \
  --terrain-camera-preset backdrop-stage --terrain-presentation backdrop \
  --terrain-backdrop-elevation 8 \
  --sun-elevation 30 --sun-azimuth -55 --debug-view surface \
  --profile-output "${profile_prefix}" --profile-warmup-frames 30
rm "${profile_video}"

durations="${profile_prefix}-terrain-surface-durations.txt"
awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' \
  "${profile_prefix}.passes.csv" | sort -n > "${durations}"
sample_count="$(wc -l < "${durations}" | tr -d ' ')"
if (( sample_count < 120 )); then
  printf 'terrain expanded backdrop study: expected 120 GPU samples, found %s\n' \
    "${sample_count}" >&2
  exit 1
fi
p95_rank="$(( (95 * sample_count + 99) / 100 ))"
p95_ms="$(sed -n "${p95_rank}p" "${durations}")"

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson focus_heights \
    "$(printf '%s\n' "${focus_heights[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson orbit_radii \
    "$(printf '%s\n' "${orbit_radii[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson sample_count "${sample_count}" \
  --argjson p95_ms "${p95_ms}" \
  '{
    schema: "cubey.terrain.directional-backdrop-expanded-capture.v1",
    cubey_commit: $commit,
    lane: "expanded-shaped",
    source: {recipe: "mountains-hierarchy-v2", seeds: $seeds},
    source_control: {recipe: "control-v2-1", seed: 9012},
    terrain: {
      outer_radius_m: 32768,
      broad_range_m: [6000, 18000],
      detail_range_m: [10000, 26000],
      floor_footprint_m: 8000,
      structure_footprint_m: 2500,
      warp_period_m: 28000,
      warp_amplitude_m: 2500
    },
    camera: {
      focus_heights_m: $focus_heights,
      default_focus_height_m: 500,
      orbit_radii_m: $orbit_radii,
      default_orbit_radius_m: 400,
      elevation_degrees: 8,
      unrestricted_yaw: true,
      azimuth_degrees: $azimuths
    },
    render: {resolution: [1920, 1080], primary_view: "surface", diagnostic_view: "clay"},
    performance: {
      resolution: [2560, 1440],
      warmup_frames: 30,
      terrain_surface_gpu_sample_count: $sample_count,
      terrain_surface_gpu_p95_ms: $p95_ms,
      terrain_surface_gpu_p95_limit_ms: 1.0,
      pass: ($p95_ms < 1.0)
    }
  }' > "${TMP_DIR}/capture-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Expanded Directional Backdrop Study

Review in this order:

1. `expanded-diagnostics-*.png`: compare base, filtered floor, and shaped height
   before slope and gate fields. The broad/detail gates should read as a wide,
   warped directional transition, never as a circular basin or a narrow wall.
2. `expanded-surface-seeds.png`: check mountain distance, transition exposure,
   foreground emptiness, and silhouette stability across all seeds and yaws.
3. `expanded-surface-focus-height.png`: isolate the `100/500/1000 m` elevated
   focus without changing terrain or orbit distance.
4. `expanded-surface-orbit-radius.png`: isolate zoom from `100 m` through
   `1 km`; the foreground should not reveal a seam or force the camera back to
   the valley floor.
5. `expanded-v2-1-surface-control.png`: determine whether any success depends
   only on hierarchy-v2 source morphology.
6. `expanded-clay-control.png`: use only to verify silhouette and occupancy
   after the surface-first review.

The previous rejected comparison remains under
`outputs/terrain/directional-backdrop-study-v1/`. This pack is a follow-up
experiment and does not promote the expanded lane into production.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'terrain expanded backdrop study: wrote %s (terrain p95 %s ms, %s samples)\n' \
  "${OUT_DIR}" "${p95_ms}" "${sample_count}"
