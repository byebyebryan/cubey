#!/usr/bin/env bash
set -euo pipefail

APP="${1:-./build/dev/projects/terrain/terrain}"
SOURCE_REPORT_APP="${2:-./build/dev/projects/terrain/terrain_source_report}"
BACKDROP_REPORT_APP="${3:-./build/dev/projects/terrain/terrain_backdrop_report}"
OUT_DIR="${4:-outputs/terrain/rendering-quality-reset}"
REF_DIR="${TERRAIN_ENGINE_REF_DIR:-/home/bryan/code/ref/TerrainEngine-OpenGL/resources}"
EXPECTED_SOURCE_SHA256="5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb"

for executable in "${APP}" "${SOURCE_REPORT_APP}" "${BACKDROP_REPORT_APP}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain rendering quality review: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in montage magick; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain rendering quality review: ImageMagick %s is required\n' "${command}" >&2
    exit 2
  fi
done

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/matrix" "${OUT_DIR}/diagnostics" "${OUT_DIR}/weathering" \
  "${OUT_DIR}/comparison" "${OUT_DIR}/native" "${OUT_DIR}/reference" \
  "${OUT_DIR}/motion" "${OUT_DIR}/profile"

capture_terrain() {
  local output="$1"
  shift
  "${APP}" --headless --frames 1 --output "${output}" "$@"
}

seeds=(0 9012 12345)
presets=(mountain upland plains)
matrix_images=()
for preset in "${presets[@]}"; do
  for seed in "${seeds[@]}"; do
    output="${OUT_DIR}/matrix/${preset}-seed-${seed}-backdrop.png"
    capture_terrain "${output}" \
      --width 640 --height 360 --terrain-seed "${seed}" --terrain-preset "${preset}" \
      --terrain-weathering local --terrain-camera-preset backdrop \
      --terrain-backdrop-profile hard-cut-v1 \
      --terrain-presentation backdrop --debug-view surface
    matrix_images+=("${output}")
  done
done

diagnostic_views=(surface clay normal shadow material-weights ambient-visibility)
diagnostic_images=()
for view in "${diagnostic_views[@]}"; do
  output="${OUT_DIR}/diagnostics/mountain-${view}.png"
  capture_terrain "${output}" \
    --width 960 --height 540 --terrain-seed 9012 --terrain-preset mountain \
    --terrain-weathering local --terrain-camera-preset oblique \
    --debug-view "${view}" --sun-elevation 18 --sun-azimuth -52
  diagnostic_images+=("${output}")
done

weathering_images=()
for mode in off local; do
  for view in clay normal shadow; do
    if [[ "${mode}" == "local" ]]; then
      source="${OUT_DIR}/diagnostics/mountain-${view}.png"
      output="${OUT_DIR}/weathering/${mode}-${view}.png"
      cp "${source}" "${output}"
    else
      output="${OUT_DIR}/weathering/${mode}-${view}.png"
      capture_terrain "${output}" \
        --width 960 --height 540 --terrain-seed 9012 --terrain-preset mountain \
        --terrain-weathering "${mode}" --terrain-camera-preset oblique \
        --debug-view "${view}" --sun-elevation 18 --sun-azimuth -52
    fi
    weathering_images+=("${output}")
  done
done

for presentation in standard backdrop; do
  capture_terrain "${OUT_DIR}/comparison/mountain-${presentation}.png" \
    --width 960 --height 540 --terrain-seed 9012 --terrain-preset mountain \
    --terrain-weathering local --terrain-camera-preset backdrop \
    --terrain-backdrop-profile hard-cut-v1 \
    --terrain-presentation "${presentation}" --debug-view surface \
    --sun-elevation 22 --sun-azimuth -55
done

showcase="${OUT_DIR}/native/mountain-backdrop-1920x1080.png"
capture_terrain "${showcase}" \
  --width 1920 --height 1080 --terrain-seed 9012 --terrain-preset mountain \
  --terrain-weathering local --terrain-camera-preset backdrop \
  --terrain-backdrop-profile hard-cut-v1 \
  --terrain-presentation backdrop --debug-view surface \
  --sun-elevation 22 --sun-azimuth -55
magick "${showcase}" -crop 960x540+0+540 +repage \
  "${OUT_DIR}/native/mountain-backdrop-foreground-crop.png"
magick "${showcase}" -crop 960x540+960+270 +repage \
  "${OUT_DIR}/native/mountain-backdrop-midground-crop.png"

"${APP}" --headless --capture video --frames 180 --fps 30 \
  --width 960 --height 540 --output "${OUT_DIR}/motion/terrain-standard-traversal.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-camera-preset surface --terrain-presentation standard --debug-view surface
"${APP}" --headless --capture video --frames 180 --fps 30 \
  --width 960 --height 540 --output "${OUT_DIR}/motion/terrain-backdrop-traversal.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-camera-preset surface --terrain-presentation backdrop --debug-view surface
"${APP}" --headless --capture video --frames 60 --fps 30 \
  --width 960 --height 540 --output "${OUT_DIR}/profile/terrain-quality-profile.mp4" \
  --terrain-seed 9012 --terrain-preset mountain --terrain-weathering local \
  --terrain-camera-preset surface --terrain-presentation backdrop --debug-view surface \
  --profile-output "${OUT_DIR}/profile/terrain-quality" --profile-warmup-frames 5

montage -label '%t' "${matrix_images[@]}" -tile 3x3 -geometry 384x216+8+24 \
  "${OUT_DIR}/terrain-quality-matrix-sheet.png"
montage -label '%t' "${diagnostic_images[@]}" -tile 3x2 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-quality-diagnostics-sheet.png"
montage -label '%t' "${weathering_images[@]}" -tile 3x2 -geometry 480x270+8+24 \
  "${OUT_DIR}/terrain-quality-weathering-sheet.png"
montage -label '%t' \
  "${OUT_DIR}/comparison/mountain-standard.png" \
  "${OUT_DIR}/comparison/mountain-backdrop.png" \
  -tile 2x1 -geometry 640x360+8+24 "${OUT_DIR}/terrain-quality-presentation-sheet.png"

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
    -tile 3x1 -geometry 480x270+8+32 "${OUT_DIR}/terrain-quality-external-reference-sheet.jpg"
  {
    printf '{\n  "role": "external visual oracle only",\n'
    printf '  "runtime_dependency": false,\n  "source_directory": "%s",\n' "${REF_DIR}"
    printf '  "files": [\n'
    for index in "${!reference_entries[@]}"; do
      entry="${reference_entries[$index]}"
      separator=','
      if [[ "${index}" -eq $((${#reference_entries[@]} - 1)) ]]; then separator=''; fi
      printf '    {"name": "%s", "sha256": "%s"}%s\n' \
        "$(basename "${entry}")" "$(sha256sum "${entry}" | awk '{print $1}')" "${separator}"
    done
    printf '  ]\n}\n'
  } > "${OUT_DIR}/reference/provenance.json"
else
  printf 'terrain rendering quality review: no TerrainEngine screenshots found in %s\n' \
    "${REF_DIR}" >&2
fi

"${SOURCE_REPORT_APP}" > "${OUT_DIR}/source-summary.json"
"${BACKDROP_REPORT_APP}" > "${OUT_DIR}/backdrop-camera-plans.json"
source_sha256="$(sha256sum "${OUT_DIR}/source-summary.json" | awk '{print $1}')"
if [[ "${source_sha256}" != "${EXPECTED_SOURCE_SHA256}" ]]; then
  printf 'terrain rendering quality review: source contract changed: expected %s, got %s\n' \
    "${EXPECTED_SOURCE_SHA256}" "${source_sha256}" >&2
  exit 1
fi

cat > "${OUT_DIR}/review-metadata.json" <<EOF
{
  "schema": "cubey.terrain.rendering-quality-reset.v1",
  "source_sha256": "${source_sha256}",
  "source_frozen": true,
  "seeds": [0, 9012, 12345],
  "presets": ["mountain", "upland", "plains"],
  "material_scales_m": {"macro": 680, "meso": 145, "local": 34},
  "landform_ambient_radius_m": 96,
  "diagnostics": ["surface", "clay", "normal", "shadow", "material-weights", "ambient-visibility"],
  "native_resolution": [1920, 1080],
  "motion": {"frames": 180, "fps": 30, "resolution": [960, 540]},
  "profile": {"frames": 60, "warmup_frames": 5, "resolution": [960, 540]},
  "external_reference": ${reference_found}
}
EOF

printf 'terrain rendering quality review written to %s\n' "${OUT_DIR}"
