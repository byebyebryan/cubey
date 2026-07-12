#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
SOURCE_REPORT_APP="${2:-./build/dev/projects/terrain/terrain_source_report}"
BACKDROP_REPORT_APP="${3:-./build/dev/projects/terrain/terrain_backdrop_report}"
OUT_DIR="${4:-outputs/terrain/backdrop-presentation}"
EXPECTED_SOURCE_SHA256="5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb"

for executable in "${APP}" "${SOURCE_REPORT_APP}" "${BACKDROP_REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain backdrop review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
if ! command -v montage >/dev/null 2>&1; then
  printf 'terrain backdrop review: ImageMagick montage is required\n' >&2
  exit 2
fi

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/matrix" "${OUT_DIR}/comparison" "${OUT_DIR}/distance" \
  "${OUT_DIR}/profile"

seeds=(0 9012 12345)
presets=(mountain upland plains)
matrix_images=()

capture_terrain() {
  local output="$1"
  shift
  "${APP}" --headless --frames 1 --output "${output}" "$@"
}

for preset in "${presets[@]}"; do
  for seed in "${seeds[@]}"; do
    output="${OUT_DIR}/matrix/${preset}-seed-${seed}-backdrop.png"
    capture_terrain "${output}" \
      --width 640 --height 360 \
      --terrain-seed "${seed}" --terrain-preset "${preset}" \
      --terrain-weathering local --terrain-camera-preset backdrop \
      --terrain-presentation backdrop --debug-view surface
    matrix_images+=("${output}")
  done
done

capture_terrain "${OUT_DIR}/comparison/mountain-standard.png" \
  --width 960 --height 540 --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset backdrop \
  --terrain-presentation standard --debug-view surface
capture_terrain "${OUT_DIR}/comparison/mountain-backdrop.png" \
  --width 960 --height 540 --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset backdrop \
  --terrain-presentation backdrop --debug-view surface
capture_terrain "${OUT_DIR}/comparison/mountain-coverage.png" \
  --width 960 --height 540 --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset backdrop \
  --terrain-presentation backdrop --debug-view vegetation-coverage

for camera in ground surface backdrop; do
  capture_terrain "${OUT_DIR}/distance/mountain-${camera}-backdrop.png" \
    --width 960 --height 540 --terrain-seed 9012 --terrain-preset mountain \
    --terrain-weathering local --terrain-camera-preset "${camera}" \
    --terrain-presentation backdrop --debug-view surface
done

capture_terrain "${OUT_DIR}/terrain-backdrop-showcase-1920x1080.png" \
  --width 1920 --height 1080 --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset backdrop \
  --terrain-presentation backdrop --debug-view surface \
  --sun-elevation 22 --sun-azimuth -55

"${APP}" --headless --capture video --frames 180 --fps 30 \
  --width 960 --height 540 \
  --output "${OUT_DIR}/terrain-backdrop-surface-traversal.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-camera-preset surface --terrain-presentation backdrop --debug-view surface

"${APP}" --headless --capture video --frames 60 --fps 30 \
  --width 960 --height 540 \
  --output "${OUT_DIR}/profile/terrain-backdrop-profile.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-camera-preset surface --terrain-presentation backdrop --debug-view surface \
  --profile-output "${OUT_DIR}/profile/terrain-backdrop" --profile-warmup-frames 5

montage -label '%t' "${matrix_images[@]}" -tile 3x3 -geometry 384x216+8+24 \
  "${OUT_DIR}/terrain-backdrop-matrix-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/comparison/mountain-standard.png" \
  "${OUT_DIR}/comparison/mountain-backdrop.png" \
  "${OUT_DIR}/comparison/mountain-coverage.png" \
  -tile 3x1 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-backdrop-comparison-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/distance/mountain-ground-backdrop.png" \
  "${OUT_DIR}/distance/mountain-surface-backdrop.png" \
  "${OUT_DIR}/distance/mountain-backdrop-backdrop.png" \
  -tile 3x1 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-backdrop-distance-sheet.png"

"${SOURCE_REPORT_APP}" > "${OUT_DIR}/source-summary.json"
"${BACKDROP_REPORT_APP}" > "${OUT_DIR}/backdrop-camera-plans.json"
source_sha256="$(sha256sum "${OUT_DIR}/source-summary.json" | awk '{print $1}')"
if [[ "${source_sha256}" != "${EXPECTED_SOURCE_SHA256}" ]]; then
  printf 'terrain backdrop review: source contract changed: expected %s, got %s\n' \
    "${EXPECTED_SOURCE_SHA256}" "${source_sha256}" >&2
  exit 1
fi

cat > "${OUT_DIR}/review-metadata.json" <<EOF
{
  "schema": "cubey.terrain.backdrop-presentation.v2",
  "source_sha256": "${source_sha256}",
  "source_frozen": true,
  "seeds": [0, 9012, 12345],
  "presets": ["mountain", "upland", "plains"],
  "camera": {"preset": "backdrop", "minimum_clearance_m": 150, "vertical_fov_degrees": 40},
  "foreground": {"clear_distance_m": 300, "safety_margin_m": 10, "sample_step_m": 25},
  "presentation": {"default": "standard", "study": "backdrop", "geometry": false},
  "distance_contract": {"supported_lower_edge_m": 300, "ground_is_negative_control": true},
  "traversal": {"camera": "surface", "frames": 180, "fps": 30, "duration_seconds": 6},
  "profile": {"frames": 60, "warmup_frames": 5, "resolution": [960, 540]}
}
EOF

printf 'terrain backdrop review written to %s\n' "${OUT_DIR}"
