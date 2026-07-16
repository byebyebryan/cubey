#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STUDY_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_directional_backdrop_study}"
OUT_DIR="${2:-${ROOT_DIR}/outputs/terrain/radial-lod-ab-v1}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

if [[ ! -x "${STUDY_APP}" ]]; then
  printf 'terrain radial LOD A/B: executable not found: %s\n' "${STUDY_APP}" >&2
  exit 2
fi
for command in awk ffmpeg jq magick sed sort; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    printf 'terrain radial LOD A/B: %s is required\n' "${command}" >&2
    exit 2
  fi
done

recipe="mountains-hierarchy-v2"
seed=9012
width=2560
height=1440
strides=(1 2 3)
azimuths=(0 120 240)
orbit_frames=(0 30 60)

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw" "${TMP_DIR}/profile"

run_video() {
  local output="$1"
  local stride="$2"
  local radius="$3"
  local view="$4"
  local profile_prefix="${5:-}"
  local profile_args=()
  if [[ -n "${profile_prefix}" ]]; then
    profile_args=(--profile-output "${profile_prefix}" --profile-warmup-frames 30)
  fi

  "${STUDY_APP}" --headless --capture video --frames 90 --fps 30 \
    --width "${width}" --height "${height}" --output "${output}" \
    --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane cached-radial --radial-render-stride "${stride}" \
    --directional-focus-height 500 --directional-orbit-radius "${radius}" \
    --terrain-camera-preset backdrop --terrain-presentation backdrop \
    --terrain-backdrop-azimuth 0 --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view "${view}" --validation \
    "${profile_args[@]}"
}

run_png() {
  local output="$1"
  local stride="$2"
  local radius="$3"
  local azimuth="$4"
  local view="$5"
  "${STUDY_APP}" --headless --frames 1 --width "${width}" --height "${height}" \
    --output "${output}" --terrain-recipe "${recipe}" --terrain-seed "${seed}" \
    --directional-lane cached-radial --radial-render-stride "${stride}" \
    --directional-focus-height 500 --directional-orbit-radius "${radius}" \
    --terrain-camera-preset backdrop --terrain-presentation backdrop \
    --terrain-backdrop-azimuth "${azimuth}" --terrain-backdrop-elevation 8 \
    --sun-elevation 30 --sun-azimuth -55 --debug-view "${view}" --validation
}

extract_orbit() {
  local video="$1"
  local output_dir="$2"
  mkdir -p "${output_dir}"
  for index in "${!orbit_frames[@]}"; do
    ffmpeg -hide_banner -loglevel error -i "${video}" \
      -vf "select=eq(n\\,${orbit_frames[index]})" -frames:v 1 \
      "${output_dir}/azimuth-${azimuths[index]}.png"
  done
}

for radius in 100 400; do
  for stride in "${strides[@]}"; do
    views=(surface)
    if [[ "${radius}" == "100" ]]; then
      views+=(projected-edge)
    fi
    for view in "${views[@]}"; do
      raw_dir="${TMP_DIR}/raw/radius-${radius}/${view}/stride-${stride}"
      video="${raw_dir}/orbit.mp4"
      profile_prefix=""
      if [[ "${radius}" == "400" && "${view}" == "surface" ]]; then
        profile_prefix="${TMP_DIR}/profile/stride-${stride}"
      fi
      mkdir -p "${raw_dir}"
      run_video "${video}" "${stride}" "${radius}" "${view}" "${profile_prefix}"
      extract_orbit "${video}" "${raw_dir}"
      rm "${video}"
    done
  done
done

focus_specs=(
  "100|120|surface|100 m stress / surface"
  "100|120|projected-edge|100 m stress / projected edge"
  "400|0|surface|400 m product / surface"
)
for spec in "${focus_specs[@]}"; do
  IFS='|' read -r radius azimuth view label <<<"${spec}"
  for stride in "${strides[@]}"; do
    output="${TMP_DIR}/raw/focus/${view}-${radius}m-yaw-${azimuth}-stride-${stride}.png"
    mkdir -p "$(dirname "${output}")"
    run_png "${output}" "${stride}" "${radius}" "${azimuth}" "${view}"
  done
done

make_sheet() {
  local output="$1"
  local radius="$2"
  local view="$3"
  local inputs=()
  for stride in "${strides[@]}"; do
    for azimuth in "${azimuths[@]}"; do
      inputs+=(
        -label "stride ${stride} / yaw ${azimuth} deg"
        "${TMP_DIR}/raw/radius-${radius}/${view}/stride-${stride}/azimuth-${azimuth}.png"
      )
    done
  done
  magick montage "${inputs[@]}" -tile 3x3 -geometry 960x540+8+24 "${output}"
}

make_silhouette_sheet() {
  local output="$1"
  local radius="$2"
  local crop_dir="${TMP_DIR}/raw/radius-${radius}/surface-crops"
  local inputs=()
  mkdir -p "${crop_dir}"
  for stride in "${strides[@]}"; do
    for azimuth in "${azimuths[@]}"; do
      source="${TMP_DIR}/raw/radius-${radius}/surface/stride-${stride}/azimuth-${azimuth}.png"
      crop="${crop_dir}/stride-${stride}-azimuth-${azimuth}.png"
      magick "${source}" -crop 2560x880+0+0 +repage "${crop}"
      inputs+=( -label "stride ${stride} / yaw ${azimuth} deg" "${crop}" )
    done
  done
  magick montage "${inputs[@]}" -tile 3x3 -geometry 960x330+8+24 "${output}"
}

make_sheet "${TMP_DIR}/radial-lod-ab-100m-surface.png" 100 surface
make_sheet "${TMP_DIR}/radial-lod-ab-100m-projected-edge.png" 100 projected-edge
make_sheet "${TMP_DIR}/radial-lod-ab-400m-surface.png" 400 surface
make_silhouette_sheet "${TMP_DIR}/radial-lod-ab-100m-silhouettes.png" 100
make_silhouette_sheet "${TMP_DIR}/radial-lod-ab-400m-silhouettes.png" 400

focus_inputs=()
focus_crop_inputs=()
for spec in "${focus_specs[@]}"; do
  IFS='|' read -r radius azimuth view label <<<"${spec}"
  for stride in "${strides[@]}"; do
    source="${TMP_DIR}/raw/focus/${view}-${radius}m-yaw-${azimuth}-stride-${stride}.png"
    crop="${TMP_DIR}/raw/focus/${view}-${radius}m-yaw-${azimuth}-stride-${stride}-crop.png"
    magick "${source}" -crop 2560x880+0+0 +repage "${crop}"
    focus_inputs+=( -label "${label} / stride ${stride}" "${source}" )
    focus_crop_inputs+=( -label "${label} / stride ${stride}" "${crop}" )
  done
done
magick montage "${focus_inputs[@]}" -tile 3x3 -geometry 960x540+8+24 \
  "${TMP_DIR}/radial-lod-ab-focused.png"
magick montage "${focus_crop_inputs[@]}" -tile 3x3 -geometry 960x330+8+24 \
  "${TMP_DIR}/radial-lod-ab-focused-crops.png"

profile_json=()
for stride in "${strides[@]}"; do
  prefix="${TMP_DIR}/profile/stride-${stride}"
  durations="${prefix}-terrain-surface-durations.txt"
  awk -F, '$2 == "gpu" && $3 == "terrain surface" { print $5 }' \
    "${prefix}.passes.csv" | sort -n > "${durations}"
  sample_count="$(wc -l < "${durations}" | tr -d ' ')"
  if (( sample_count == 0 )); then
    printf 'terrain radial LOD A/B: no GPU samples for stride %s\n' "${stride}" >&2
    exit 1
  fi
  p50_rank="$(( (50 * sample_count + 99) / 100 ))"
  p95_rank="$(( (95 * sample_count + 99) / 100 ))"
  mean_ms="$(awk '{ sum += $1 } END { printf "%.6f", sum / NR }' "${durations}")"
  p50_ms="$(sed -n "${p50_rank}p" "${durations}")"
  p95_ms="$(sed -n "${p95_rank}p" "${durations}")"
  profile_json+=("$(jq -n \
    --argjson stride "${stride}" \
    --argjson sample_count "${sample_count}" \
    --argjson mean_ms "${mean_ms}" \
    --argjson p50_ms "${p50_ms}" \
    --argjson p95_ms "${p95_ms}" \
    '{stride: $stride, sample_count: $sample_count, mean_ms: $mean_ms,
      p50_ms: $p50_ms, p95_ms: $p95_ms}')")
done

printf '%s\n' "${profile_json[@]}" | jq -s \
  --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
  --arg recipe "${recipe}" \
  --argjson seed "${seed}" \
  '{commit: $commit, recipe: $recipe, seed: $seed,
    render_size: [2560, 1440], profile_radius_m: 400,
    camera: {focus_height_m: 500, elevation_degrees: 8}, profiles: .}' \
  > "${TMP_DIR}/metadata.json"

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'terrain radial LOD A/B: wrote %s\n' "${OUT_DIR}"
