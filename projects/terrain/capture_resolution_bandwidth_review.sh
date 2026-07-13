#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
SOURCE_REPORT_APP="${2:-./build/dev/projects/terrain/terrain_source_report}"
OUT_DIR="${3:-outputs/terrain/resolution-bandwidth-prototype}"
REF_DIR="${TERRAIN_ENGINE_REF_DIR:-/home/bryan/code/ref/TerrainEngine-OpenGL/resources}"
EXPECTED_V1_SHA256="5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb"
EXPECTED_V2_SHA256="c9b1f9b94d7f2d14f8f301df59c29651207c279b43f31339815e552421b2b456"

for executable in "${APP}" "${SOURCE_REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain resolution review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in montage magick; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain resolution review: ImageMagick %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/comparison" "${OUT_DIR}/diagnostics" "${OUT_DIR}/seeds" \
  "${OUT_DIR}/native" "${OUT_DIR}/motion" "${OUT_DIR}/profile" \
  "${OUT_DIR}/reference"

capture() {
  local output="$1"
  local render_path="$2"
  local source_version="$3"
  local view="$4"
  local width="${5:-960}"
  local height="${6:-540}"
  local seed="${7:-9012}"
  "${APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-seed "${seed}" --terrain-preset mountain \
    --terrain-weathering local --terrain-render-path "${render_path}" \
    --terrain-source-version "${source_version}" --terrain-target-edge-px 4 \
    --terrain-camera-preset backdrop --terrain-presentation backdrop \
    --debug-view "${view}" --sun-elevation 22 --sun-azimuth -55 --validation
}

comparison_images=()
for variant in control-v1 quality-v1 quality-v2; do
  render_path="${variant%-*}"
  source_version="${variant#*-}"
  output="${OUT_DIR}/comparison/${variant}.png"
  capture "${output}" "${render_path}" "${source_version}" surface
  comparison_images+=("${output}")
done

seed_images=()
for seed in 0 9012 12345; do
  output="${OUT_DIR}/seeds/quality-v2-seed-${seed}.png"
  capture "${output}" quality v2 surface 640 360 "${seed}"
  seed_images+=("${output}")
done

diagnostic_images=()
capture "${OUT_DIR}/diagnostics/control-v1-material-albedo.png" control v1 material-albedo
for view in clay tessellation-factor projected-edge source-bands material-albedo \
  material-normal material-weights shadow; do
  output="${OUT_DIR}/diagnostics/quality-v2-${view}.png"
  capture "${output}" quality v2 "${view}"
  diagnostic_images+=("${output}")
done

for variant in control-v1 quality-v1 quality-v2; do
  render_path="${variant%-*}"
  source_version="${variant#*-}"
  output="${OUT_DIR}/native/${variant}-1920x1080.png"
  capture "${output}" "${render_path}" "${source_version}" surface 1920 1080
  magick "${output}" -crop 960x540+960+270 +repage \
    "${OUT_DIR}/native/${variant}-midground-crop.png"
done

"${APP}" --headless --capture video --frames 90 --fps 30 --width 960 --height 540 \
  --output "${OUT_DIR}/motion/quality-v2-traversal.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-render-path quality --terrain-source-version v2 --terrain-target-edge-px 4 \
  --terrain-camera-preset surface --terrain-presentation backdrop --debug-view surface

"${APP}" --headless --capture video --frames 60 --fps 30 --width 960 --height 540 \
  --output "${OUT_DIR}/profile/quality-v2-profile.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-render-path quality --terrain-source-version v2 --terrain-target-edge-px 4 \
  --terrain-camera-preset surface --terrain-presentation backdrop --debug-view surface \
  --profile-output "${OUT_DIR}/profile/quality-v2" --profile-warmup-frames 5

montage -label '%t' "${comparison_images[@]}" -tile 3x1 -geometry 640x360+8+24 \
  "${OUT_DIR}/terrain-resolution-comparison-sheet.png"
montage -label '%t' "${seed_images[@]}" -tile 3x1 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-resolution-seed-sheet.png"
montage -label '%t' "${diagnostic_images[@]}" -tile 4x2 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-resolution-diagnostics-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/diagnostics/control-v1-material-albedo.png" \
  "${OUT_DIR}/diagnostics/quality-v2-material-albedo.png" \
  -tile 2x1 -geometry 640x360+8+24 "${OUT_DIR}/terrain-resolution-material-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/native/control-v1-midground-crop.png" \
  "${OUT_DIR}/native/quality-v1-midground-crop.png" \
  "${OUT_DIR}/native/quality-v2-midground-crop.png" \
  -tile 3x1 -geometry 640x360+8+24 "${OUT_DIR}/terrain-resolution-native-crops-sheet.png"

reference_found=false
reference_entries=()
for name in pic.jpg pic2.jpg pic3.jpg; do
  if [[ -f "${REF_DIR}/${name}" ]]; then
    output="${OUT_DIR}/reference/external-terrain-engine-${name}"
    cp "${REF_DIR}/${name}" "${output}"
    reference_entries+=("${output}")
    reference_found=true
  fi
done
if [[ "${reference_found}" == true ]]; then
  montage -label 'EXTERNAL REFERENCE: %t' "${reference_entries[@]}" \
    -tile 3x1 -geometry 480x270+8+32 "${OUT_DIR}/terrain-resolution-reference-sheet.jpg"
  {
    printf '{\n  "role": "external visual oracle only",\n'
    printf '  "runtime_dependency": false,\n  "source_directory": "%s",\n' "${REF_DIR}"
    printf '  "files": [\n'
    for index in "${!reference_entries[@]}"; do
      separator=','
      if [[ "${index}" -eq $((${#reference_entries[@]} - 1)) ]]; then separator=''; fi
      printf '    {"name": "%s", "sha256": "%s"}%s\n' \
        "$(basename "${reference_entries[index]}")" \
        "$(sha256sum "${reference_entries[index]}" | awk '{print $1}')" "${separator}"
    done
    printf '  ]\n}\n'
  } > "${OUT_DIR}/reference/provenance.json"
fi

"${SOURCE_REPORT_APP}" > "${OUT_DIR}/source-v1-summary.json"
"${SOURCE_REPORT_APP}" --source-version v2 > "${OUT_DIR}/source-v2-summary.json"
v1_sha256="$(sha256sum "${OUT_DIR}/source-v1-summary.json" | awk '{print $1}')"
v2_sha256="$(sha256sum "${OUT_DIR}/source-v2-summary.json" | awk '{print $1}')"
if [[ "${v1_sha256}" != "${EXPECTED_V1_SHA256}" || "${v2_sha256}" != "${EXPECTED_V2_SHA256}" ]]; then
  printf 'terrain resolution review: source hash mismatch: v1=%s v2=%s\n' \
    "${v1_sha256}" "${v2_sha256}" >&2
  exit 1
fi

control_energy="$(magick "${OUT_DIR}/diagnostics/control-v1-material-albedo.png" \
  -crop 480x270+480+135 +repage -colorspace Gray -morphology Convolve Laplacian:0 \
  -evaluate Abs 0 -format '%[fx:mean]' info:)"
quality_energy="$(magick "${OUT_DIR}/diagnostics/quality-v2-material-albedo.png" \
  -crop 480x270+480+135 +repage -colorspace Gray -morphology Convolve Laplacian:0 \
  -evaluate Abs 0 -format '%[fx:mean]' info:)"
energy_ratio="$(awk -v control="${control_energy}" -v quality="${quality_energy}" \
  'BEGIN { if (control > 0) printf "%.4f", quality / control; else print "0" }')"
incremental_frame_ms="$(awk -F, '
  $3 == "headless.before_frame" {
    if (count > 0) total += $4 - previous
    previous = $4
    count += 1
  }
  END { if (count > 1) printf "%.4f", total / (count - 1); else print "0" }
' "${OUT_DIR}/profile/quality-v2.passes.csv")"
if ! awk -v ratio="${energy_ratio}" -v frame_ms="${incremental_frame_ms}" \
  'BEGIN { exit !(ratio >= 2.0 && frame_ms > 0.0 && frame_ms < 33.3) }'; then
  printf 'terrain resolution review: acceptance failed: material ratio=%s frame_ms=%s\n' \
    "${energy_ratio}" "${incremental_frame_ms}" >&2
  exit 1
fi

cat > "${OUT_DIR}/review-metadata.json" <<EOF
{
  "schema": "cubey.terrain.resolution-bandwidth-prototype.v1",
  "source_sha256": {"v1": "${v1_sha256}", "v2": "${v2_sha256}"},
  "default_contract": {"render_path": "control", "source_version": "v1"},
  "quality_contract": {"target_edge_px": 4, "max_tessellation": 64},
  "material_tiles": {"count": 4, "extent": 1024, "period_m": 256, "mip_levels": 11},
  "material_albedo_laplacian_energy": {"control_v1": ${control_energy}, "quality_v2": ${quality_energy}, "ratio": ${energy_ratio}},
  "incremental_frame_ms": ${incremental_frame_ms},
  "seeds": [0, 9012, 12345],
  "native_resolution": [1920, 1080],
  "motion": {"frames": 90, "fps": 30, "resolution": [960, 540]},
  "profile": {"frames": 60, "warmup_frames": 5, "resolution": [960, 540]},
  "external_reference": ${reference_found}
}
EOF

printf 'terrain resolution and bandwidth review written to %s\n' "${OUT_DIR}"
