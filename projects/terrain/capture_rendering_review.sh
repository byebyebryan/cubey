#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
REF_APP="${2:-./build/dev-terrain-studies/studies/terrain/reference/terrain_reference}"
OUT_DIR="${3:-outputs/terrain/rendering-refinement}"
REPORT_APP="${4:-./build/dev/projects/terrain/terrain_source_report}"
EXPECTED_SOURCE_SHA256="5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb"

for executable in "${APP}" "${REF_APP}" "${REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain rendering review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
if ! command -v montage >/dev/null 2>&1; then
  printf 'terrain rendering review: ImageMagick montage is required\n' >&2
  exit 2
fi

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/clay" "${OUT_DIR}/final" "${OUT_DIR}/diagnostics" \
  "${OUT_DIR}/ground" "${OUT_DIR}/control" "${OUT_DIR}/lod" \
  "${OUT_DIR}/profile"

seeds=(0 9012 12345)
presets=(mountain upland plains)
sun_azimuths=(-70 0 70)
clay_images=()
final_images=()
diagnostic_images=()
ground_images=()

capture_terrain() {
  local output="$1"
  shift
  "${APP}" --headless --frames 1 --output "${output}" "$@"
}

for seed in "${seeds[@]}"; do
  for azimuth in "${sun_azimuths[@]}"; do
    capture_terrain \
      "${OUT_DIR}/clay/mountain-seed-${seed}-sun-${azimuth}.png" \
      --width 640 --height 360 \
      --terrain-seed "${seed}" --terrain-preset mountain \
      --terrain-weathering local --terrain-camera-preset oblique \
      --debug-view clay --sun-elevation 12 --sun-azimuth "${azimuth}"
    clay_images+=("${OUT_DIR}/clay/mountain-seed-${seed}-sun-${azimuth}.png")
  done
done

for preset in "${presets[@]}"; do
  for seed in "${seeds[@]}"; do
    capture_terrain \
      "${OUT_DIR}/final/${preset}-seed-${seed}-oblique.png" \
      --width 640 --height 360 \
      --terrain-seed "${seed}" --terrain-preset "${preset}" \
      --terrain-weathering local --terrain-camera-preset oblique \
      --debug-view surface
    final_images+=("${OUT_DIR}/final/${preset}-seed-${seed}-oblique.png")
  done
done

for camera in oblique ground; do
  for view in surface clay shadow aerial-transmittance; do
    capture_terrain \
      "${OUT_DIR}/diagnostics/mountain-${camera}-${view}.png" \
      --width 640 --height 360 \
      --terrain-seed 9012 --terrain-preset mountain \
      --terrain-weathering local --terrain-camera-preset "${camera}" \
      --debug-view "${view}" --sun-elevation 12 --sun-azimuth 0
    diagnostic_images+=("${OUT_DIR}/diagnostics/mountain-${camera}-${view}.png")
  done
done

for cell_size in 2 1; do
  for preset in "${presets[@]}"; do
    capture_terrain \
      "${OUT_DIR}/ground/${preset}-cell-${cell_size}m.png" \
      --width 640 --height 360 \
      --terrain-seed 9012 --terrain-preset "${preset}" \
      --terrain-weathering local --terrain-camera-preset ground \
      --terrain-cell-size "${cell_size}" --debug-view surface
    ground_images+=("${OUT_DIR}/ground/${preset}-cell-${cell_size}m.png")
  done
done

capture_terrain "${OUT_DIR}/lod/mountain-ground-lod.png" \
  --width 640 --height 360 \
  --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset ground --debug-view lod

capture_terrain "${OUT_DIR}/control/terrain-v1-mountain-oblique.png" \
  --width 640 --height 360 \
  --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset oblique --debug-view surface
"${REF_APP}" --headless --frames 1 --width 640 --height 360 \
  --output "${OUT_DIR}/control/terrain-engine-ref-oblique.png" \
  --terrain-seed 9012 --terrain-recipe terrain-engine-ref \
  --terrain-camera-preset oblique --terrain-preview-color height \
  --no-terrain-water-surface

"${APP}" --headless --capture video --frames 240 --fps 30 \
  --width 960 --height 540 \
  --output "${OUT_DIR}/terrain-rendering-ground-traversal.mp4" \
  --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset ground --debug-view surface

"${APP}" --headless --capture video --frames 60 --fps 30 \
  --width 960 --height 540 \
  --output "${OUT_DIR}/profile/terrain-rendering-profile.mp4" \
  --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset ground --debug-view surface \
  --profile-output "${OUT_DIR}/profile/terrain-rendering" \
  --profile-warmup-frames 5

montage -label '%t' "${clay_images[@]}" -tile 3x3 -geometry 384x216+8+24 \
  "${OUT_DIR}/terrain-rendering-clay-sun-sheet.png"
montage -label '%t' "${final_images[@]}" -tile 3x3 -geometry 384x216+8+24 \
  "${OUT_DIR}/terrain-rendering-final-sheet.png"
montage -label '%t' "${diagnostic_images[@]}" -tile 4x2 -geometry 384x216+8+24 \
  "${OUT_DIR}/terrain-rendering-diagnostics-sheet.png"
montage -label '%t' "${ground_images[@]}" -tile 3x2 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-rendering-ground-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/control/terrain-engine-ref-oblique.png" \
  "${OUT_DIR}/control/terrain-v1-mountain-oblique.png" \
  -tile 2x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-rendering-control-sheet.png"

"${REPORT_APP}" > "${OUT_DIR}/source-summary.json"
source_sha256="$(sha256sum "${OUT_DIR}/source-summary.json" | awk '{print $1}')"
if [[ "${source_sha256}" != "${EXPECTED_SOURCE_SHA256}" ]]; then
  printf 'terrain rendering review: source contract changed: expected %s, got %s\n' \
    "${EXPECTED_SOURCE_SHA256}" "${source_sha256}" >&2
  exit 1
fi

cat > "${OUT_DIR}/review-metadata.json" <<EOF
{
  "schema": "cubey.terrain.rendering-refinement.v1",
  "source_sha256": "${source_sha256}",
  "source_frozen": true,
  "seeds": [0, 9012, 12345],
  "presets": ["mountain", "upland", "plains"],
  "clay_sun": {"elevation_degrees": 12, "azimuth_degrees": [-70, 0, 70]},
  "final_sun": {"elevation_degrees": 38, "azimuth_degrees": -42},
  "ground": {"clearance_m": 2, "speed_mps": 12, "near_cell_sizes_m": [2, 1]},
  "traversal": {"frames": 240, "fps": 30, "duration_seconds": 8, "distance_m": 96},
  "profile": {"frames": 60, "warmup_frames": 5, "resolution": [960, 540]},
  "source_control": "terrain-engine-ref"
}
EOF

printf 'terrain rendering review written to %s\n' "${OUT_DIR}"
