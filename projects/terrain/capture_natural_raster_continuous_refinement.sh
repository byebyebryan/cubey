#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NATURAL_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_natural_raster_study}"
REPORT_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_natural_raster_report}"
OUT_DIR="${3:-${ROOT_DIR}/outputs/terrain/terrain-diffusion-continuous-refinement-v1}"
FIELD_DIR="${CUBEY_TERRAIN_DIFFUSION_FIELDS:-${ROOT_DIR}/outputs/terrain/.terrain-diffusion-bakeoff-v1-fields}"
GENERATOR="${ROOT_DIR}/projects/terrain/tools/run_terrain_diffusion_bake.sh"
TMP_DIR="${OUT_DIR}.tmp.$$"
EXPECTED_CODE_REVISION="82a0431281f21a6ec3d691a12ee61525de5b0790"
EXPECTED_MODEL_REVISION="9ef8030cb805b433b98ec25c5dddefbac07a9e26"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${NATURAL_APP}" "${REPORT_APP}" "${GENERATOR}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain continuous refinement: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in ffmpeg jq magick realpath sha256sum; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain continuous refinement: %s is required\n' "${command}" >&2
    exit 2
  fi
done

seeds=(0 9012 12345)
lanes=(split-log-500 uniform-500 uniform-750)
azimuths=(0 60 120 180 240 300)
frames=(0 15 30 45 60 75)

valid_generated_fields() {
  local field_root="$1"
  local report="${field_root}/generation-report.json"
  [[ -f "${report}" ]] || return 1
  jq -e \
    --arg code "${EXPECTED_CODE_REVISION}" \
    --arg model "${EXPECTED_MODEL_REVISION}" '
    .schema == "cubey.terrain.diffusion-bakeoff-generation.v1" and
    .source.code_revision == $code and
    .source.model_revision == $model and
    .source.settings.latents_batch_size == 1 and
    .source.settings.process_rng_seeding == "seed-value-v1" and
    .export_contract_revision == 3 and
    .field_contract.seeds == [0, 9012, 12345] and
    .field_contract.size == [2048, 2048] and
    .field_contract.sample_spacing_m == 30 and
    (.fields | length) == 3
  ' "${report}" >/dev/null || return 1

  local manifest relative expected actual
  while IFS=$'\t' read -r relative expected; do
    manifest="${field_root}/${relative}"
    [[ -f "${manifest}" ]] || return 1
    actual="$(sha256sum "${manifest}" | awk '{print $1}')"
    [[ "${actual}" == "${expected}" ]] || return 1
  done < <(jq -r '.fields[] | [.manifest, .manifest_sha256] | @tsv' "${report}")
}

if [[ "${CUBEY_TERRAIN_DIFFUSION_REGENERATE:-0}" == "1" ]] || \
  ! valid_generated_fields "${FIELD_DIR}"; then
  "${GENERATOR}" --output-dir "${FIELD_DIR}"
fi
if ! valid_generated_fields "${FIELD_DIR}"; then
  printf 'terrain continuous refinement: generated field validation failed: %s\n' \
    "${FIELD_DIR}" >&2
  exit 1
fi

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/clay" "${TMP_DIR}/raw/surface" \
  "${TMP_DIR}/raw/diagnostics"
ln -s "$(realpath --relative-to "${TMP_DIR}" "${FIELD_DIR}")" "${TMP_DIR}/generated"

for seed in "${seeds[@]}"; do
  for focus in 500 750; do
    report="${TMP_DIR}/reports/seed-${seed}-focus-${focus}.json"
    "${REPORT_APP}" --terrain-study-field "${FIELD_DIR}/fields/seed-${seed}" \
      --natural-focus-height "${focus}" > "${report}"
    if ! jq -e --argjson focus "${focus}" '
      .schema == "cubey.terrain.natural-raster-stage.v1" and
      .support.centered_search_covered == true and
      .support.natural_selection_covered == true and
      .source.width == 2048 and .source.height == 2048 and
      .source.sample_spacing_m == 30 and
      .natural.placement.contract_satisfied == true and
      .natural.stage.contract_satisfied == true and
      .natural.stage.source_focus_xz_m == .natural.placement.source_focus_xz_m and
      .natural.stage.showcase_yaw_radians == (-.natural.placement.mountain_yaw_radians) and
      .natural.requested_focus_height_m == $focus and
      .natural.stage.focus_height_m == $focus and
      .natural.stage.minimum_camera_clearance_m >= 10
    ' "${report}" >/dev/null; then
      printf 'terrain continuous refinement: contract failed for seed %s at %s m\n' \
        "${seed}" "${focus}" >&2
      exit 1
    fi
  done
  if ! jq -e --slurpfile raised "${TMP_DIR}/reports/seed-${seed}-focus-750.json" '
    .natural.stage.source_focus_xz_m == $raised[0].natural.stage.source_focus_xz_m and
    .natural.stage.showcase_yaw_radians == $raised[0].natural.stage.showcase_yaw_radians
  ' "${TMP_DIR}/reports/seed-${seed}-focus-500.json" >/dev/null; then
    printf 'terrain continuous refinement: focus-height A/B moved seed %s\n' "${seed}" >&2
    exit 1
  fi
done

common_args=(
  --terrain-render-path backdrop
  --terrain-backdrop-mode detached
  --terrain-backdrop-center continuous
  --terrain-backdrop-min-distance 3200
  --terrain-backdrop-mesh-density high
  --terrain-camera-preset backdrop-stage
  --terrain-presentation backdrop
  --terrain-backdrop-orbit-radius 100
  --terrain-backdrop-elevation 8
  --terrain-weathering off
  --sun-elevation 30
  --sun-azimuth -55
  --validation
)

run_lane() {
  local lane="$1"
  shift
  local -a lane_args=()
  case "${lane}" in
    split-log-500)
      lane_args=(--natural-center-sampling split-log --natural-focus-height 500)
      ;;
    uniform-500)
      lane_args=(--natural-center-sampling uniform --natural-focus-height 500)
      ;;
    uniform-750)
      lane_args=(--natural-center-sampling uniform --natural-focus-height 750)
      ;;
    *)
      printf 'terrain continuous refinement: unknown lane: %s\n' "${lane}" >&2
      exit 2
      ;;
  esac
  "${NATURAL_APP}" "$@" "${common_args[@]}" "${lane_args[@]}"
}

capture_orbit() {
  local lane="$1"
  local seed="$2"
  local view="$3"
  local lane_dir="${TMP_DIR}/raw/${view}/${lane}/seed-${seed}"
  local video="${lane_dir}/orbit.mp4"
  mkdir -p "${lane_dir}"

  run_lane "${lane}" --headless --capture video --frames 90 --fps 30 \
    --width 1920 --height 1080 --output "${video}" \
    --terrain-seed "${seed}" \
    --terrain-study-field "${FIELD_DIR}/fields/seed-${seed}" --debug-view "${view}"

  if [[ "${view}" == "clay" ]]; then
    for index in "${!frames[@]}"; do
      ffmpeg -hide_banner -loglevel error -i "${video}" \
        -vf "select=eq(n\\,${frames[index]})" -frames:v 1 \
        "${lane_dir}/relative-azimuth-${azimuths[index]}.png"
    done
  else
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf 'select=eq(n\,0)' -frames:v 1 "${lane_dir}/showcase.png"
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf 'select=eq(n\,45)' -frames:v 1 "${lane_dir}/opposite.png"
  fi
  rm "${video}"
}

for seed in "${seeds[@]}"; do
  for lane in "${lanes[@]}"; do
    capture_orbit "${lane}" "${seed}" clay
    capture_orbit "${lane}" "${seed}" surface
  done
done

for lane in "${lanes[@]}"; do
  for view in projected-edge normal; do
    output="${TMP_DIR}/raw/diagnostics/${lane}-${view}-seed-12345.png"
    run_lane "${lane}" --headless --frames 1 --width 1920 --height 1080 \
      --output "${output}" --terrain-seed 12345 \
      --terrain-study-field "${FIELD_DIR}/fields/seed-12345" --debug-view "${view}"
  done
done

for seed in "${seeds[@]}"; do
  clay_inputs=()
  surface_inputs=()
  for lane in "${lanes[@]}"; do
    for azimuth in "${azimuths[@]}"; do
      clay_inputs+=(
        -label "${lane} / +${azimuth} deg"
        "${TMP_DIR}/raw/clay/${lane}/seed-${seed}/relative-azimuth-${azimuth}.png"
      )
    done
    for facing in showcase opposite; do
      surface_inputs+=(
        -label "${lane} / ${facing}"
        "${TMP_DIR}/raw/surface/${lane}/seed-${seed}/${facing}.png"
      )
    done
  done
  magick montage "${clay_inputs[@]}" -tile 6x3 -geometry 480x270+8+24 \
    "${TMP_DIR}/clay-seed-${seed}.png"
  magick montage "${surface_inputs[@]}" -tile 2x3 -geometry 800x450+8+24 \
    "${TMP_DIR}/surface-seed-${seed}.png"
done

diagnostic_inputs=()
for view in projected-edge normal; do
  for lane in "${lanes[@]}"; do
    diagnostic_inputs+=(
      -label "${lane} / ${view}"
      "${TMP_DIR}/raw/diagnostics/${lane}-${view}-seed-12345.png"
    )
  done
done
magick montage "${diagnostic_inputs[@]}" -tile 3x2 -geometry 640x360+8+24 \
  "${TMP_DIR}/diagnostics-seed-12345.png"

field_hashes="$(jq '[.fields[] | {seed, manifest_sha256}]' \
  "${FIELD_DIR}/generation-report.json")"
jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --arg generation_report_sha256 "$(sha256sum "${FIELD_DIR}/generation-report.json" | awk '{print $1}')" \
  --argjson field_hashes "${field_hashes}" \
  --argjson lanes "$(printf '%s\n' "${lanes[@]}" | jq -R . | jq -s .)" \
  --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" '
  {
    schema: "cubey.terrain.natural-raster-continuous-refinement.v1",
    cubey_commit: $commit,
    generation_report: "generated/generation-report.json",
    generation_report_sha256: $generation_report_sha256,
    field_hashes: $field_hashes,
    lanes: $lanes,
    seeds: $seeds,
    relative_azimuth_degrees: $azimuths,
    source: {size: [2048, 2048], sample_spacing_m: 30, height_shaping: false},
    geometry: {
      angular_intervals: 3072,
      center_intervals: 96,
      outer_intervals: 768,
      source_samples: 2657280,
      vertices: 2694289,
      render_triangles: 5305344,
      split_log: {
        inner_extent_m: [0, 300], inner_spacing_m: 9.375,
        transition_extent_m: [300, 3200]
      },
      uniform: {extent_m: [0, 3200], spacing_m: 33.3333333333},
      outer: {extent_m: [3200, 16384], distribution: "logarithmic"}
    },
    render: {
      resolution: [1920, 1080], profile: "hard-cut-v1",
      center: "continuous", visible_extent_m: [0, 16384],
      mesh_density: "high", render_stride: 1,
      orbit_radius_m: 100, orbit_elevation_degrees: 8,
      weathering: "off", sun_elevation_degrees: 30, sun_azimuth_degrees: -55
    },
    focus_policy: {default_m: 500, comparison_m: 750, promoted: false}
  }
' > "${TMP_DIR}/capture-metadata.json"

jq -s '
  {
    schema: "cubey.terrain.natural-raster-continuous-refinement-summary.v1",
    fields: [range(0; length; 2) as $index | {
      seed: .[$index].source.seed,
      focus_xz_m: .[$index].natural.stage.source_focus_xz_m,
      showcase_yaw_radians: .[$index].natural.stage.showcase_yaw_radians,
      focus_500: {
        setup_ms: .[$index].natural.setup_ms,
        clearance_m: .[$index].natural.stage.minimum_camera_clearance_m
      },
      focus_750: {
        setup_ms: .[$index + 1].natural.setup_ms,
        clearance_m: .[$index + 1].natural.stage.minimum_camera_clearance_m
      }
    }]
  }
' "${TMP_DIR}/reports/seed-0-focus-500.json" \
  "${TMP_DIR}/reports/seed-0-focus-750.json" \
  "${TMP_DIR}/reports/seed-9012-focus-500.json" \
  "${TMP_DIR}/reports/seed-9012-focus-750.json" \
  "${TMP_DIR}/reports/seed-12345-focus-500.json" \
  "${TMP_DIR}/reports/seed-12345-focus-750.json" \
  > "${TMP_DIR}/contract-summary.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Natural Raster Continuous Refinement v1

Review in this order:

1. `capture-metadata.json` confirms unchanged raster hashes and identical
   geometry budgets. Only center-ring placement and focus height vary.
2. `clay-seed-*.png` compares all three lanes over six unrestricted yaws. The
   uniform 500 m row should reduce broad radial shoulders without weakening
   mountain silhouettes relative to split-log 500 m.
3. `surface-seed-*.png` compares showcase and opposite directions. Treat
   split-log versus uniform at 500 m as the geometry verdict; compare the two
   uniform rows only for camera composition.
4. `diagnostics-seed-12345.png` isolates projected edge span and normals on the
   seed that exposed the coarse foreground most clearly.
5. `contract-summary.json` verifies that 500/750 m use identical source
   placement and records setup timing plus camera clearance.

The pack does not filter, reshape, resample, or regenerate the source. It does
not tune material shading. The 750 m focus is an A/B lane and is not promoted.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'terrain continuous refinement: wrote %s\n' "${OUT_DIR}"
