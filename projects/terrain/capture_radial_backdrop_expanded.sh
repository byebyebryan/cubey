#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_study}"
REPORT="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_report}"
OUT_DIR="${3:-${ROOT_DIR}/outputs/terrain/radial-backdrop-expanded-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${APP}" "${REPORT}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain radial backdrop study: executable not found: %s\n' \
      "${executable}" >&2
    exit 2
  fi
done
for command in ffmpeg jq magick; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain radial backdrop study: %s is required\n' "${command}" >&2
    exit 2
  fi
done

recipe="mountains-hierarchy-v2"
seeds=(0 9012 12345)
azimuths=(0 60 120 180 240 300)
frames=(0 15 30 45 60 75)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/orbits"

for seed in "${seeds[@]}"; do
  "${REPORT}" --output-dir "${TMP_DIR}/reports/seed-${seed}" \
    --recipe "${recipe}" --seed "${seed}" --grid-size 1024 \
    --radial --focus-height 500
done

capture_orbit() {
  local lane="$1"
  local seed="$2"
  local view="$3"
  local lane_dir="${TMP_DIR}/raw/orbits/${lane}/seed-${seed}/${view}"
  local video="${lane_dir}/orbit.mp4"
  mkdir -p "${lane_dir}"
  "${APP}" --headless --capture video --frames 90 --fps 30 \
    --width 1920 --height 1080 --output "${video}" \
    --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane "${lane}" --directional-focus-height 500 \
    --directional-orbit-radius 400 \
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
  capture_orbit expanded-radial "${seed}" surface
done
capture_orbit expanded-radial 9012 clay
capture_orbit expanded-shaped 9012 surface

make_orbit_sheet() {
  local output="$1"
  shift
  local specs=("$@")
  local inputs=()
  for spec in "${specs[@]}"; do
    read -r label lane seed view <<<"${spec}"
    for azimuth in "${azimuths[@]}"; do
      inputs+=(
        -label "${label} / ${azimuth} deg"
        "${TMP_DIR}/raw/orbits/${lane}/seed-${seed}/${view}/azimuth-${azimuth}.png"
      )
    done
  done
  magick montage "${inputs[@]}" -tile 6x"${#specs[@]}" -geometry 480x270+8+24 \
    "${output}"
}

make_orbit_sheet "${TMP_DIR}/radial-surface-seeds.png" \
  "seed-0 expanded-radial 0 surface" \
  "seed-9012 expanded-radial 9012 surface" \
  "seed-12345 expanded-radial 12345 surface"
make_orbit_sheet "${TMP_DIR}/radial-vs-directional-surface.png" \
  "radial expanded-radial 9012 surface" \
  "directional expanded-shaped 9012 surface"
make_orbit_sheet "${TMP_DIR}/radial-clay-control.png" \
  "radial-clay expanded-radial 9012 clay"

for seed in "${seeds[@]}"; do
  report_dir="${TMP_DIR}/reports/seed-${seed}"
  diagnostic_inputs=()
  for field in base-height floor-height shaped-height base-slope shaped-slope \
    broad-gate detail-gate; do
    diagnostic_inputs+=( -label "${field}" "${report_dir}/${field}.png" )
  done
  magick montage "${diagnostic_inputs[@]}" -tile 4x2 -geometry 384x384+8+24 \
    "${TMP_DIR}/radial-diagnostics-seed-${seed}.png"
done

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" \
  '{
    schema: "cubey.terrain.radial-backdrop-expanded-capture.v1",
    cubey_commit: $commit,
    lane: "expanded-radial",
    control_lane: "expanded-shaped",
    source: {recipe: "mountains-hierarchy-v2", seeds: $seeds},
    terrain: {
      outer_radius_m: 32768,
      floor_footprint_m: 8000,
      structure_footprint_m: 2500,
      broad_range_m: [6000, 24000],
      detail_range_m: [12000, 29000],
      transition: "unwarped-radial"
    },
    camera: {
      focus_height_m: 500,
      orbit_radius_m: 400,
      elevation_degrees: 8,
      unrestricted_yaw: true,
      azimuth_degrees: $azimuths
    },
    render: {resolution: [1920, 1080], primary_view: "surface", diagnostic_view: "clay"}
  }' > "${TMP_DIR}/capture-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Expanded Radial Backdrop Study

Review in this order:

1. `radial-vs-directional-surface.png`: compare the same source, seed, camera,
   and six unrestricted headings. Look for a uniform mountain wall in radial
   and the exposed uplift shelf or empty headings in directional.
2. `radial-surface-seeds.png`: check whether the radial transition remains
   plausible across source morphology rather than only on seed 9012.
3. `radial-diagnostics-seed-*.png`: the gates are intentionally circular.
   Determine whether their broad 18 km and 17 km bands create a visible ring
   in shaped height or slope despite blending filtered versions of one source.
4. `radial-clay-control.png`: isolate silhouette and occupancy from material
   and atmosphere only after the surface review.

This is a study lane. It does not replace the cached production backdrop or
promote radial shaping into the terrain source contract.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'terrain radial backdrop study: wrote %s\n' "${OUT_DIR}"
