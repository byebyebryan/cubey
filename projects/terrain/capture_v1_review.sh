#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
REF_APP="${2:-./build/dev/projects/terrain_ref/terrain_ref}"
OUT_DIR="${3:-outputs/terrain/v1-reboot}"
REPORT_APP="${4:-./build/dev/projects/terrain/terrain_source_report}"

for executable in "${APP}" "${REF_APP}" "${REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain v1 review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
if ! command -v montage >/dev/null 2>&1; then
  printf 'terrain v1 review: ImageMagick montage is required\n' >&2
  exit 2
fi

mkdir -p "${OUT_DIR}/shape" "${OUT_DIR}/presentation" "${OUT_DIR}/surface" \
  "${OUT_DIR}/weathering" "${OUT_DIR}/control"
rm -f "${OUT_DIR}"/shape/*.png "${OUT_DIR}"/presentation/*.png \
  "${OUT_DIR}"/surface/*.png "${OUT_DIR}"/weathering/*.png \
  "${OUT_DIR}"/control/*.png "${OUT_DIR}"/terrain-v1-*-sheet.png

seeds=(0 9012 12345)
presets=(mountain upland plains)
shape_images=()
presentation_images=()
surface_images=()
weathering_images=()

capture_terrain() {
  local output="$1"
  shift
  "${APP}" --headless --frames 1 --output "${output}" "$@"
}

for preset in "${presets[@]}"; do
  for seed in "${seeds[@]}"; do
    capture_terrain "${OUT_DIR}/shape/${preset}-seed-${seed}-top-height.png" \
      --width 512 --height 512 \
      --terrain-seed "${seed}" --terrain-preset "${preset}" \
      --terrain-weathering off --terrain-camera-preset top --debug-view height
    shape_images+=("${OUT_DIR}/shape/${preset}-seed-${seed}-top-height.png")
    capture_terrain "${OUT_DIR}/presentation/${preset}-seed-${seed}-oblique.png" \
      --width 640 --height 360 \
      --terrain-seed "${seed}" --terrain-preset "${preset}" \
      --terrain-weathering local --terrain-camera-preset oblique --debug-view surface
    presentation_images+=("${OUT_DIR}/presentation/${preset}-seed-${seed}-oblique.png")
  done

  capture_terrain "${OUT_DIR}/surface/${preset}-seed-9012-surface.png" \
    --width 640 --height 360 \
    --terrain-seed 9012 --terrain-preset "${preset}" \
    --terrain-weathering local --terrain-camera-preset surface --debug-view surface
  surface_images+=("${OUT_DIR}/surface/${preset}-seed-9012-surface.png")
  capture_terrain "${OUT_DIR}/weathering/${preset}-clean-surface-height.png" \
    --width 640 --height 360 \
    --terrain-seed 9012 --terrain-preset "${preset}" \
    --terrain-weathering off --terrain-camera-preset surface --debug-view height
  capture_terrain "${OUT_DIR}/weathering/${preset}-weathered-surface-height.png" \
    --width 640 --height 360 \
    --terrain-seed 9012 --terrain-preset "${preset}" \
    --terrain-weathering local --terrain-camera-preset surface --debug-view height
  capture_terrain "${OUT_DIR}/weathering/${preset}-surface-weathering-delta.png" \
    --width 640 --height 360 \
    --terrain-seed 9012 --terrain-preset "${preset}" \
    --terrain-weathering local --terrain-camera-preset surface --debug-view weathering
  weathering_images+=(
    "${OUT_DIR}/weathering/${preset}-clean-surface-height.png"
    "${OUT_DIR}/weathering/${preset}-weathered-surface-height.png"
    "${OUT_DIR}/weathering/${preset}-surface-weathering-delta.png"
  )
done

capture_terrain "${OUT_DIR}/control/terrain-v1-mountain-oblique.png" \
  --width 640 --height 360 \
  --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset oblique --debug-view surface
"${REF_APP}" --headless --frames 1 --width 640 --height 360 \
  --output "${OUT_DIR}/control/terrain-engine-ref-oblique.png" \
  --terrain-seed 9012 --terrain-recipe terrain-engine-ref \
  --terrain-camera-preset oblique --terrain-preview-color height \
  --no-terrain-water-surface

montage -label '%t' "${shape_images[@]}" -tile 3x3 -geometry 320x320+8+24 \
  "${OUT_DIR}/terrain-v1-shape-sheet.png"
montage -label '%t' "${presentation_images[@]}" -tile 3x3 -geometry 384x216+8+24 \
  "${OUT_DIR}/terrain-v1-presentation-sheet.png"
montage -label '%t' "${surface_images[@]}" -tile 3x1 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-v1-surface-sheet.png"
montage -label '%t' "${weathering_images[@]}" -tile 3x3 -geometry 384x216+8+24 \
  "${OUT_DIR}/terrain-v1-weathering-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/control/terrain-engine-ref-oblique.png" \
  "${OUT_DIR}/control/terrain-v1-mountain-oblique.png" \
  -tile 2x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-v1-control-sheet.png"

printf '%s\n' \
  '{' \
  '  "schema": "cubey.terrain.v1.review",' \
  '  "seeds": [0, 9012, 12345],' \
  '  "presets": ["mountain", "upland", "plains"],' \
  '  "weathering_modes": ["off", "local"],' \
  '  "lod": {"levels": 8, "cells_per_axis": 128, "near_cell_size_m": 2.0, "outer_half_extent_m": 16384.0},' \
  '  "source_control": "terrain-engine-ref"' \
  '}' > "${OUT_DIR}/review-metadata.json"
"${REPORT_APP}" > "${OUT_DIR}/source-summary.json"

printf 'terrain v1 review written to %s\n' "${OUT_DIR}"
