#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
SOURCE_REPORT_APP="${2:-./build/dev/projects/terrain/terrain_source_report}"
BACKDROP_REPORT_APP="${3:-./build/dev/projects/terrain/terrain_backdrop_report}"
OUT_DIR="${4:-outputs/terrain/midground-detail-v3}"
EXPECTED_V1_SHA256="5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb"
EXPECTED_V2_SHA256="c9b1f9b94d7f2d14f8f301df59c29651207c279b43f31339815e552421b2b456"

for executable in "${APP}" "${SOURCE_REPORT_APP}" "${BACKDROP_REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain midground review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in magick montage; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain midground review: ImageMagick %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/matrix/backdrop" "${OUT_DIR}/matrix/midground" \
  "${OUT_DIR}/diagnostics" "${OUT_DIR}/identity" "${OUT_DIR}/motion" \
  "${OUT_DIR}/profile"

capture() {
  local output="$1"
  local detail="$2"
  local camera="$3"
  local seed="$4"
  local view="${5:-surface}"
  local width="${6:-1920}"
  local height="${7:-1080}"
  "${APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering local --terrain-render-path quality \
    --terrain-source-version v2 --terrain-surface-detail "${detail}" \
    --terrain-target-edge-px 4 --terrain-camera-preset "${camera}" \
    --terrain-presentation backdrop --debug-view "${view}" \
    --sun-elevation 22 --sun-azimuth -55 --validation
}

backdrop_images=()
for seed in 0 9012 12345; do
  for detail in tile layered; do
    output="${OUT_DIR}/matrix/backdrop/seed-${seed}-${detail}.png"
    capture "${output}" "${detail}" backdrop "${seed}"
    backdrop_images+=("${output}")
  done
done

midground_images=()
for seed in 9012 12345; do
  for detail in tile layered; do
    output="${OUT_DIR}/matrix/midground/seed-${seed}-${detail}.png"
    capture "${output}" "${detail}" midground "${seed}"
    midground_images+=("${output}")
  done
done

for detail in tile layered; do
  for view in normal material-normal; do
    capture "${OUT_DIR}/diagnostics/${detail}-${view}.png" "${detail}" midground 9012 \
      "${view}" 960 540
  done
done
layered_diagnostics=()
for view in height material-albedo material-roughness material-height material-cavity \
  material-weights; do
  output="${OUT_DIR}/diagnostics/layered-${view}.png"
  capture "${output}" layered midground 9012 "${view}" 960 540
  layered_diagnostics+=("${output}")
done

for detail in tile layered; do
  capture "${OUT_DIR}/identity/${detail}-height.png" "${detail}" midground 9012 height 960 540
done

profile_capture() {
  local detail="$1"
  "${APP}" --headless --capture video --frames 60 --fps 30 --width 960 --height 540 \
    --output "${OUT_DIR}/profile/${detail}-profile.mp4" \
    --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
    --terrain-render-path quality --terrain-source-version v2 \
    --terrain-surface-detail "${detail}" --terrain-target-edge-px 4 \
    --terrain-camera-preset backdrop --terrain-presentation backdrop \
    --debug-view surface --sun-elevation 22 --sun-azimuth -55 \
    --profile-output "${OUT_DIR}/profile/${detail}" --profile-warmup-frames 5 \
    --validation
}
profile_capture tile
profile_capture layered

"${APP}" --headless --capture video --frames 90 --fps 30 --width 960 --height 540 \
  --output "${OUT_DIR}/motion/layered-midground-traversal.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-render-path quality --terrain-source-version v2 \
  --terrain-surface-detail layered --terrain-target-edge-px 4 \
  --terrain-camera-preset midground --terrain-presentation backdrop \
  --debug-view surface --sun-elevation 22 --sun-azimuth -55 --validation

montage -label '%t' "${backdrop_images[@]}" -tile 2x3 -geometry 960x540+8+24 \
  "${OUT_DIR}/terrain-midground-detail-backdrop-sheet.png"
montage -label '%t' "${midground_images[@]}" -tile 2x2 -geometry 960x540+8+24 \
  "${OUT_DIR}/terrain-midground-detail-midground-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/diagnostics/tile-normal.png" \
  "${OUT_DIR}/diagnostics/layered-normal.png" \
  "${OUT_DIR}/diagnostics/tile-material-normal.png" \
  "${OUT_DIR}/diagnostics/layered-material-normal.png" \
  -tile 2x2 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-midground-detail-normal-sheet.png"
montage -label '%t' "${layered_diagnostics[@]}" -tile 3x2 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-midground-detail-diagnostics-sheet.png"

"${SOURCE_REPORT_APP}" > "${OUT_DIR}/source-v1-summary.json"
"${SOURCE_REPORT_APP}" --source-version v2 > "${OUT_DIR}/source-v2-summary.json"
"${BACKDROP_REPORT_APP}" > "${OUT_DIR}/backdrop-camera-summary.json"
v1_sha256="$(sha256sum "${OUT_DIR}/source-v1-summary.json" | awk '{print $1}')"
v2_sha256="$(sha256sum "${OUT_DIR}/source-v2-summary.json" | awk '{print $1}')"
if [[ "${v1_sha256}" != "${EXPECTED_V1_SHA256}" || \
      "${v2_sha256}" != "${EXPECTED_V2_SHA256}" ]]; then
  printf 'terrain midground review: source hash mismatch: v1=%s v2=%s\n' \
    "${v1_sha256}" "${v2_sha256}" >&2
  exit 1
fi

height_difference_pixels="$(magick compare -metric AE \
  "${OUT_DIR}/identity/tile-height.png" \
  "${OUT_DIR}/identity/layered-height.png" null: 2>&1 | awk '{print $1}' || true)"
if [[ "${height_difference_pixels}" != "0" ]]; then
  printf 'terrain midground review: height identity failed: %s changed pixels\n' \
    "${height_difference_pixels}" >&2
  exit 1
fi

laplacian_energy() {
  magick "$1" -crop 480x270+240+135 +repage -colorspace Gray \
    -morphology Convolve Laplacian:0 -evaluate Abs 0 -format '%[fx:mean]' info:
}
tile_normal_energy="$(laplacian_energy \
  "${OUT_DIR}/diagnostics/tile-material-normal.png")"
layered_normal_energy="$(laplacian_energy \
  "${OUT_DIR}/diagnostics/layered-material-normal.png")"
normal_energy_ratio="$(awk -v tile="${tile_normal_energy}" -v layered="${layered_normal_energy}" \
  'BEGIN { if (tile > 0) printf "%.4f", layered / tile; else print "0" }')"

observed_frame_interval_ms() {
  awk -F, '
    $3 == "headless.before_frame" {
      if (count > 0) total += $4 - previous
      previous = $4
      count += 1
    }
    END { if (count > 1) printf "%.4f", total / (count - 1); else print "0" }
  ' "$1"
}
tile_frame_ms="$(observed_frame_interval_ms "${OUT_DIR}/profile/tile.passes.csv")"
layered_frame_ms="$(observed_frame_interval_ms "${OUT_DIR}/profile/layered.passes.csv")"
frame_delta_ms="$(awk -v tile="${tile_frame_ms}" -v layered="${layered_frame_ms}" \
  'BEGIN { printf "%.4f", layered - tile }')"
tile_memory_mib="$(awk -F, 'NR > 1 && $6 > max { max = $6 } END { printf "%.3f", max }' \
  "${OUT_DIR}/profile/tile.frames.csv")"
layered_memory_mib="$(awk -F, 'NR > 1 && $6 > max { max = $6 } END { printf "%.3f", max }' \
  "${OUT_DIR}/profile/layered.frames.csv")"
memory_delta_mib="$(awk -v tile="${tile_memory_mib}" -v layered="${layered_memory_mib}" \
  'BEGIN { printf "%.3f", layered - tile }')"

if ! awk -v ratio="${normal_energy_ratio}" -v frame_ms="${layered_frame_ms}" \
  -v memory="${layered_memory_mib}" \
  'BEGIN { exit !(ratio >= 1.25 && ratio <= 3.0 && frame_ms > 0.0 && frame_ms < 33.3 && memory < 128.0) }'; then
  printf 'terrain midground review: acceptance failed: normal ratio=%s frame_ms=%s memory_mib=%s\n' \
    "${normal_energy_ratio}" "${layered_frame_ms}" "${layered_memory_mib}" >&2
  exit 1
fi

cat > "${OUT_DIR}/review-metadata.json" <<EOF
{
  "schema": "cubey.terrain.midground-detail-review.v1",
  "source_sha256": {"v1": "${v1_sha256}", "v2": "${v2_sha256}"},
  "comparison": {"control": "quality + tile", "candidate": "quality + layered"},
  "geometry_identity": {"changed_height_pixels": ${height_difference_pixels}},
  "material_layers": {"count": 4, "extent": 1024, "period_m": 256, "mip_levels": 11, "requested_anisotropy": 8},
  "material_normal_laplacian_energy": {"tile": ${tile_normal_energy}, "layered": ${layered_normal_energy}, "ratio": ${normal_energy_ratio}},
  "observed_frame_interval_ms": {"tile": ${tile_frame_ms}, "layered": ${layered_frame_ms}, "delta": ${frame_delta_ms}},
  "device_local_usage_mib": {"tile": ${tile_memory_mib}, "layered": ${layered_memory_mib}, "delta": ${memory_delta_mib}},
  "views": {"backdrop_seeds": [0, 9012, 12345], "midground_seeds": [9012, 12345], "native_resolution": [1920, 1080]},
  "motion": {"frames": 90, "fps": 30, "resolution": [960, 540]},
  "profile": {"frames": 60, "warmup_frames": 5, "resolution": [960, 540]},
  "acceptance": {"height_identity": true, "normal_energy_ratio_range": [1.25, 3.0], "frame_budget_ms": 33.3, "device_local_usage_limit_mib": 128}
}
EOF

printf 'terrain midground detail review written to %s\n' "${OUT_DIR}"
