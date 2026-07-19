#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STRICT_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_external_source_study}"
NATURAL_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_natural_raster_study}"
REPORT_APP="${3:-${ROOT_DIR}/build/dev/projects/terrain/terrain_natural_raster_report}"
OUT_DIR="${4:-${ROOT_DIR}/outputs/terrain/terrain-diffusion-stage-v1}"
FIELD_DIR="${CUBEY_TERRAIN_DIFFUSION_FIELDS:-${ROOT_DIR}/outputs/terrain/.terrain-diffusion-bakeoff-v1-fields}"
GENERATOR="${ROOT_DIR}/projects/terrain/tools/run_terrain_diffusion_bake.sh"
TMP_DIR="${OUT_DIR}.tmp.$$"
EXPECTED_CODE_REVISION="82a0431281f21a6ec3d691a12ee61525de5b0790"
EXPECTED_MODEL_REVISION="9ef8030cb805b433b98ec25c5dddefbac07a9e26"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${STRICT_APP}" "${NATURAL_APP}" "${REPORT_APP}" "${GENERATOR}"; do
  if [[ ! -x "${executable}" ]]; then
    printf 'terrain natural raster stage: executable not found: %s\n' "${executable}" >&2
    exit 2
  fi
done
for command in ffmpeg jq magick realpath sha256sum; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain natural raster stage: %s is required\n' "${command}" >&2
    exit 2
  fi
done

seeds=(0 9012 12345)
lanes=(strict-cutout natural-cutout natural-continuous)
candidate_lanes=(natural-cutout natural-continuous)
azimuths=(0 60 120 180 240 300)
frames=(0 15 30 45 60 75)

valid_generated_fields() {
  local field_root="${1}"
  local report="${field_root}/generation-report.json"
  [[ -f "${report}" ]] || return 1
  jq -e \
    --arg code "${EXPECTED_CODE_REVISION}" \
    --arg model "${EXPECTED_MODEL_REVISION}" \
    '.schema == "cubey.terrain.diffusion-bakeoff-generation.v1" and
     .source.code_revision == $code and
     .source.model_revision == $model and
     .source.settings.latents_batch_size == 1 and
     .source.settings.process_rng_seeding == "seed-value-v1" and
     .export_contract_revision == 3 and
     .field_contract.seeds == [0, 9012, 12345] and
     .field_contract.size == [2048, 2048] and
     .field_contract.sample_spacing_m == 30 and
     (.fields | length) == 3' "${report}" >/dev/null || return 1

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
  printf 'terrain natural raster stage: generated field validation failed: %s\n' \
    "${FIELD_DIR}" >&2
  exit 1
fi

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/clay" "${TMP_DIR}/raw/surface" \
  "${TMP_DIR}/raw/stage-ownership" "${TMP_DIR}/raw/placement"
ln -s "$(realpath --relative-to "${TMP_DIR}" "${FIELD_DIR}")" "${TMP_DIR}/generated"

for seed in "${seeds[@]}"; do
  report="${TMP_DIR}/reports/seed-${seed}.json"
  "${REPORT_APP}" \
    --terrain-study-field "${FIELD_DIR}/fields/seed-${seed}" > "${report}"
  if ! jq -e '
    .schema == "cubey.terrain.natural-raster-stage.v1" and
    .support.centered_search_covered == true and
    .support.strict_selection_covered == true and
    .support.natural_selection_covered == true and
    .source.width == 2048 and .source.height == 2048 and
    .source.sample_spacing_m == 30 and
    .natural.placement.contract_satisfied == true and
    .natural.stage.contract_satisfied == true and
    .natural.stage.source_focus_xz_m == .natural.placement.source_focus_xz_m and
    .natural.stage.showcase_yaw_radians == (-.natural.placement.mountain_yaw_radians) and
    .natural.stage.focus_height_m == .natural.requested_focus_height_m and
    .natural.stage.minimum_camera_clearance_m >= 10 and
    (.natural.placement.mountain_sector_count >= 4 and
     .natural.placement.mountain_sector_count <= 14) and
    .natural.placement.largest_mountain_arc_sectors >= 3 and
    .natural.placement.largest_open_arc_sectors >= 4
  ' "${report}" >/dev/null; then
    printf 'terrain natural raster stage: contract failed for seed %s\n' "${seed}" >&2
    exit 1
  fi
done

jq -s '
  {schema: "cubey.terrain.natural-raster-stage-summary.v1",
   fields: map({
     seed: .source.seed,
     strict: {
       focus_xz_m: .strict.stage.source_focus_xz_m,
       focus_height_m: .strict.stage.focus_height_m,
       contract_satisfied: .strict.stage.contract_satisfied
     },
     natural: {
       focus_xz_m: .natural.stage.source_focus_xz_m,
       focus_height_m: .natural.stage.focus_height_m,
       minimum_camera_clearance_m: .natural.stage.minimum_camera_clearance_m,
       mountain_yaw_radians: .natural.placement.mountain_yaw_radians,
       showcase_yaw_radians: .natural.stage.showcase_yaw_radians,
       mountain_sector_count: .natural.placement.mountain_sector_count,
       open_sector_count: .natural.placement.open_sector_count,
       largest_mountain_arc_sectors: .natural.placement.largest_mountain_arc_sectors,
       largest_open_arc_sectors: .natural.placement.largest_open_arc_sectors,
       contract_satisfied: .natural.stage.contract_satisfied
     }
   })}
' "${TMP_DIR}/reports/seed-0.json" "${TMP_DIR}/reports/seed-9012.json" \
  "${TMP_DIR}/reports/seed-12345.json" > "${TMP_DIR}/contract-summary.json"

common_args=(
  --terrain-render-path backdrop
  --terrain-backdrop-mode detached
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
    strict-cutout)
      "${STRICT_APP}" "$@" "${common_args[@]}"
      ;;
    natural-cutout)
      lane_args=(--terrain-backdrop-center consumer-owned)
      "${NATURAL_APP}" "$@" "${common_args[@]}" "${lane_args[@]}"
      ;;
    natural-continuous)
      lane_args=(--terrain-backdrop-center continuous)
      "${NATURAL_APP}" "$@" "${common_args[@]}" "${lane_args[@]}"
      ;;
    *)
      printf 'terrain natural raster stage: unknown lane: %s\n' "${lane}" >&2
      exit 2
      ;;
  esac
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

for seed in "${seeds[@]}"; do
  for lane in "${candidate_lanes[@]}"; do
    output="${TMP_DIR}/raw/stage-ownership/${lane}-seed-${seed}.png"
    run_lane "${lane}" --headless --frames 1 --width 1920 --height 1080 \
      --output "${output}" --terrain-seed "${seed}" \
      --terrain-study-field "${FIELD_DIR}/fields/seed-${seed}" \
      --debug-view stage-ownership
  done
done

placement_inputs=()
for seed in "${seeds[@]}"; do
  manifest="${FIELD_DIR}/fields/seed-${seed}/manifest.json"
  report="${TMP_DIR}/reports/seed-${seed}.json"
  read -r origin_x origin_z spacing width height < <(jq -r \
    '[.grid.sample_origin_x_m, .grid.sample_origin_z_m, .grid.sample_spacing_m,
      .grid.width, .grid.height] | @tsv' "${manifest}")
  read -r natural_x natural_z strict_x strict_z yaw support < <(jq -r \
    '[.natural.stage.source_focus_xz_m[0], .natural.stage.source_focus_xz_m[1],
      .strict.stage.source_focus_xz_m[0], .strict.stage.source_focus_xz_m[1],
      .natural.placement.mountain_yaw_radians, .support.selected_radius_m] | @tsv' \
    "${report}")
  read -r natural_px natural_py strict_px strict_py support_px arrow_x arrow_y < <(awk \
    -v nx="${natural_x}" -v nz="${natural_z}" -v sx="${strict_x}" -v sz="${strict_z}" \
    -v ox="${origin_x}" -v oz="${origin_z}" -v spacing="${spacing}" \
    -v support="${support}" -v yaw="${yaw}" '
      BEGIN {
        npx = (nx - ox) / spacing
        npy = (nz - oz) / spacing
        spx = (sx - ox) / spacing
        spy = (sz - oz) / spacing
        radius = support / spacing
        printf "%.0f %.0f %.0f %.0f %.0f %.0f %.0f\n",
          npx, npy, spx, spy, radius, npx + sin(yaw) * 420, npy - cos(yaw) * 420
      }
    ')
  output="${TMP_DIR}/raw/placement/seed-${seed}.png"
  magick "${FIELD_DIR}/fields/seed-${seed}/height.png" -colorspace sRGB \
    -fill none -stroke white -strokewidth 5 \
    -draw "rectangle 3,3 $((width - 4)),$((height - 4))" \
    -stroke lime -strokewidth 8 \
    -draw "circle ${natural_px},${natural_py} $((natural_px + support_px)),${natural_py}" \
    -stroke cyan -strokewidth 12 \
    -draw "line $((natural_px - 18)),${natural_py} $((natural_px + 18)),${natural_py}" \
    -draw "line ${natural_px},$((natural_py - 18)) ${natural_px},$((natural_py + 18))" \
    -stroke orange -strokewidth 12 \
    -draw "line ${natural_px},${natural_py} ${arrow_x},${arrow_y}" \
    -fill orange -stroke none -draw "circle ${arrow_x},${arrow_y} $((arrow_x + 14)),${arrow_y}" \
    -fill none -stroke magenta -strokewidth 12 \
    -draw "line $((strict_px - 18)),$((strict_py - 18)) $((strict_px + 18)),$((strict_py + 18))" \
    -draw "line $((strict_px - 18)),$((strict_py + 18)) $((strict_px + 18)),$((strict_py - 18))" \
    -resize 1024x1024 "${output}"
  placement_inputs+=( -label "seed ${seed}" "${output}" )
done
magick montage "${placement_inputs[@]}" -tile 3x1 -geometry 640x640+8+24 \
  "${TMP_DIR}/placement-contact-sheet.png"

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

ownership_inputs=()
for seed in "${seeds[@]}"; do
  for lane in "${candidate_lanes[@]}"; do
    ownership_inputs+=(
      -label "${lane} / seed ${seed}"
      "${TMP_DIR}/raw/stage-ownership/${lane}-seed-${seed}.png"
    )
  done
done
magick montage "${ownership_inputs[@]}" -tile 2x3 -geometry 640x360+8+24 \
  "${TMP_DIR}/center-ownership-contact-sheet.png"

jq -n \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --arg generation_report_sha256 "$(sha256sum "${FIELD_DIR}/generation-report.json" | awk '{print $1}')" \
  --argjson lanes "$(printf '%s\n' "${lanes[@]}" | jq -R . | jq -s .)" \
  --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
  --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" '
  {
    schema: "cubey.terrain.natural-raster-stage-capture.v1",
    cubey_commit: $commit,
    generation_report: "generated/generation-report.json",
    generation_report_sha256: $generation_report_sha256,
    lanes: $lanes,
    seeds: $seeds,
    relative_azimuth_degrees: $azimuths,
    source: {size: [2048, 2048], sample_spacing_m: 30, height_shaping: false},
    render: {
      resolution: [1920, 1080], profile: "hard-cut-v1",
      visible_extent_m: [3200, 16384], mesh_density: "high", render_stride: 1,
      orbit_radius_m: 100, orbit_elevation_degrees: 8,
      weathering: "off", sun_elevation_degrees: 30, sun_azimuth_degrees: -55
    }
  }
' > "${TMP_DIR}/capture-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Natural Raster Stage v1

Review in this order:

1. `contract-summary.json` compares strict and natural stage placement. The
   natural lane must retain a 500 m focus and pass on every seed.
2. `placement-contact-sheet.png` checks source-space selection before render
   judgment. White is the finite field edge, lime the selected render support,
   cyan the natural focus, orange the mountain direction, and magenta the
   strict focus. Selection must remain inside the unchanged source.
3. `clay-seed-*.png` compares silhouettes at six relative, unrestricted yaws.
   Rows are strict cutout, natural cutout, and natural continuous.
4. `surface-seed-*.png` compares the stage showcase direction and its opposite.
   Look for restored mountain scale, a useful open direction, foreground
   intersections, exposed 30 m detail, and annular seams.
5. `center-ownership-contact-sheet.png` isolates cutout versus continuous
   center geometry using the stage-ownership diagnostic.

The candidate changes placement and focus height only. It does not reshape,
filter, resample, or regenerate the Terrain Diffusion fields. Promotion and
close-terrain fidelity remain outside this evidence pack.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'terrain natural raster stage: wrote %s\n' "${OUT_DIR}"
