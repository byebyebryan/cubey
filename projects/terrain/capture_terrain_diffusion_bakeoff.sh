#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONTROL_APP="${1:-${ROOT_DIR}/build/dev/projects/terrain/terrain_source_study}"
REPORT_APP="${2:-${ROOT_DIR}/build/dev/projects/terrain/terrain_source_study_report}"
EXTERNAL_APP="${3:-${ROOT_DIR}/build/dev/projects/terrain/terrain_external_source_study}"
OUT_DIR="${4:-${ROOT_DIR}/outputs/terrain/terrain-diffusion-bakeoff-v1}"
FIELD_DIR="${CUBEY_TERRAIN_DIFFUSION_FIELDS:-${ROOT_DIR}/outputs/terrain/.terrain-diffusion-bakeoff-v1-fields}"
REPEAT_FIELD_DIR="${CUBEY_TERRAIN_DIFFUSION_REPEAT_FIELDS:-${ROOT_DIR}/outputs/terrain/.terrain-diffusion-repeat-check}"
GENERATOR="${ROOT_DIR}/projects/terrain/tools/run_terrain_diffusion_bake.sh"
TMP_DIR="${OUT_DIR}.tmp.$$"
EXPECTED_CODE_REVISION="82a0431281f21a6ec3d691a12ee61525de5b0790"
EXPECTED_MODEL_REVISION="9ef8030cb805b433b98ec25c5dddefbac07a9e26"
trap 'rm -rf "${TMP_DIR}"' EXIT

for executable in "${CONTROL_APP}" "${REPORT_APP}" "${EXTERNAL_APP}" "${GENERATOR}"; do
    if [[ ! -x "${executable}" ]]; then
        printf 'terrain diffusion bakeoff: executable not found: %s\n' "${executable}" >&2
        exit 2
    fi
done
for command in ffmpeg git jq magick realpath sha256sum; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'terrain diffusion bakeoff: %s is required\n' "${command}" >&2
        exit 2
    fi
done

seeds=(0 9012 12345)
lanes=(control-v2-1 mountains-hierarchy-v2 terrain-diffusion-30m)
azimuths=(0 120 240)
frames=(0 30 60)
climate_channels=(temperature temperature_variability precipitation precipitation_variability)

valid_generated_fields() {
    local field_root="${1:-${FIELD_DIR}}"
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
    printf 'terrain diffusion bakeoff: generated field validation failed: %s\n' \
        "${FIELD_DIR}" >&2
    exit 1
fi

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/reports" "${TMP_DIR}/raw/clay" \
    "${TMP_DIR}/raw/presentation" "${TMP_DIR}/raw/seams"
ln -s "$(realpath --relative-to "${TMP_DIR}" "${FIELD_DIR}")" "${TMP_DIR}/generated"

if valid_generated_fields "${REPEAT_FIELD_DIR}"; then
    repeat_hashes="${TMP_DIR}/repeat-hashes.tsv"
    : > "${repeat_hashes}"
    for seed in "${seeds[@]}"; do
        for payload in elevation climate; do
            first="$(sha256sum "${FIELD_DIR}/fields/seed-${seed}/${payload}.f32" | awk '{print $1}')"
            second="$(sha256sum "${REPEAT_FIELD_DIR}/fields/seed-${seed}/${payload}.f32" | awk '{print $1}')"
            if [[ "${first}" != "${second}" ]]; then
                printf 'terrain diffusion bakeoff: repeat mismatch for seed %s %s\n' \
                    "${seed}" "${payload}" >&2
                exit 1
            fi
            printf '%s\t%s\t%s\n' "${seed}" "${payload}" "${first}" >> "${repeat_hashes}"
        done
    done
    jq -Rn \
        '[inputs | split("\t") | {seed: .[0] | tonumber, payload: .[1], sha256: .[2]}] |
         {schema: "cubey.terrain.diffusion-repeat-validation.v1",
          checked: true, passed: true, files: .}' \
        < "${repeat_hashes}" > "${TMP_DIR}/repeat-validation.json"
    rm "${repeat_hashes}"
else
    jq -n \
        '{schema: "cubey.terrain.diffusion-repeat-validation.v1",
          checked: false, passed: null, files: []}' \
        > "${TMP_DIR}/repeat-validation.json"
fi

for recipe in control-v2-1 mountains-hierarchy-v2; do
    "${REPORT_APP}" --output-dir "${TMP_DIR}/reports/${recipe}" --grid-size 1024 \
        --recipe "${recipe}"
done

capture_orbit() {
    local lane="$1"
    local seed="$2"
    local view="$3"
    local group="$4"
    local lane_dir="${TMP_DIR}/raw/${group}/${lane}/seed-${seed}"
    local video="${lane_dir}/orbit.mp4"
    local -a common_args
    mkdir -p "${lane_dir}"

    common_args=(
        --headless --capture video --frames 90 --fps 30
        --width 1920 --height 1080 --output "${video}"
        --terrain-seed "${seed}"
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8
        --sun-elevation 30 --sun-azimuth -55 --debug-view "${view}" --validation
    )
    if [[ "${lane}" == "terrain-diffusion-30m" ]]; then
        "${EXTERNAL_APP}" "${common_args[@]}" \
            --terrain-study-field "${FIELD_DIR}/fields/seed-${seed}"
    else
        "${CONTROL_APP}" "${common_args[@]}" --terrain-recipe "${lane}"
    fi

    for index in "${!frames[@]}"; do
        ffmpeg -hide_banner -loglevel error -i "${video}" \
            -vf "select=eq(n\\,${frames[index]})" -frames:v 1 \
            "${lane_dir}/azimuth-${azimuths[index]}.png"
    done
    rm "${video}"
}

for lane in "${lanes[@]}"; do
    for seed in "${seeds[@]}"; do
        capture_orbit "${lane}" "${seed}" clay clay
    done
    capture_orbit "${lane}" 9012 surface presentation
done

height_inputs=()
slope_inputs=()
for lane in "${lanes[@]}"; do
    for seed in "${seeds[@]}"; do
        if [[ "${lane}" == "terrain-diffusion-30m" ]]; then
            height="${FIELD_DIR}/fields/seed-${seed}/height.png"
            slope="${FIELD_DIR}/fields/seed-${seed}/slope.png"
        else
            height="${TMP_DIR}/reports/${lane}/fields/${lane}/seed-${seed}-height.png"
            slope="${TMP_DIR}/reports/${lane}/fields/${lane}/seed-${seed}-slope.png"
        fi
        height_inputs+=(-label "${lane} / ${seed}" "${height}")
        slope_inputs+=(-label "${lane} / ${seed}" "${slope}")
    done
done
magick montage "${height_inputs[@]}" -tile 3x3 -geometry 512x512+8+24 \
    "${TMP_DIR}/height-contact-sheet.png"
magick montage "${slope_inputs[@]}" -tile 3x3 -geometry 512x512+8+24 \
    "${TMP_DIR}/slope-contact-sheet.png"

selection_inputs=()
for seed in "${seeds[@]}"; do
    selection_inputs+=(
        -label "terrain-diffusion-30m / ${seed}"
        "${FIELD_DIR}/fields/seed-${seed}/selection.png"
    )
done
magick montage "${selection_inputs[@]}" -tile 3x1 -geometry 512x512+8+24 \
    "${TMP_DIR}/selection-contact-sheet.png"

climate_inputs=()
for channel in "${climate_channels[@]}"; do
    for seed in "${seeds[@]}"; do
        climate_inputs+=(
            -label "${channel} / ${seed}"
            "${FIELD_DIR}/fields/seed-${seed}/climate-${channel}.png"
        )
    done
done
magick montage "${climate_inputs[@]}" -tile 3x4 -geometry 384x384+8+24 \
    "${TMP_DIR}/climate-contact-sheet.png"

for seed in "${seeds[@]}"; do
    source_height="${FIELD_DIR}/fields/seed-${seed}/height.png"
    vertical="${TMP_DIR}/raw/seams/seed-${seed}-vertical.png"
    horizontal="${TMP_DIR}/raw/seams/seed-${seed}-horizontal.png"
    magick "${source_height}" -crop 32x2048+1008+0 +repage -resize 256x1024! "${vertical}"
    magick "${source_height}" -crop 2048x32+0+1008 +repage -resize 1024x256! "${horizontal}"
done
seam_inputs=()
for seed in "${seeds[@]}"; do
    seam_inputs+=(
        -label "vertical seam / ${seed}" "${TMP_DIR}/raw/seams/seed-${seed}-vertical.png"
        -label "horizontal seam / ${seed}" "${TMP_DIR}/raw/seams/seed-${seed}-horizontal.png"
    )
done
magick montage "${seam_inputs[@]}" -tile 2x3 -geometry 512x512+8+24 \
    "${TMP_DIR}/seam-contact-sheet.png"

for seed in "${seeds[@]}"; do
    clay_inputs=()
    for lane in "${lanes[@]}"; do
        for azimuth in "${azimuths[@]}"; do
            clay_inputs+=(
                -label "${lane} / ${azimuth} deg"
                "${TMP_DIR}/raw/clay/${lane}/seed-${seed}/azimuth-${azimuth}.png"
            )
        done
    done
    magick montage "${clay_inputs[@]}" -tile 3x3 -geometry 640x360+8+24 \
        "${TMP_DIR}/clay-seed-${seed}.png"
done

presentation_inputs=()
for lane in "${lanes[@]}"; do
    for azimuth in "${azimuths[@]}"; do
        presentation_inputs+=(
            -label "${lane} / ${azimuth} deg"
            "${TMP_DIR}/raw/presentation/${lane}/seed-9012/azimuth-${azimuth}.png"
        )
    done
done
magick montage "${presentation_inputs[@]}" -tile 3x3 -geometry 640x360+8+24 \
    "${TMP_DIR}/presentation-seed-9012.png"

jq -s \
    '{schema: "cubey.terrain.external-generator-source-summary.v1",
      controls: [.[0].recipes[0], .[1].recipes[0]],
      external: .[2]}' \
    "${TMP_DIR}/reports/control-v2-1/source-report.json" \
    "${TMP_DIR}/reports/mountains-hierarchy-v2/source-report.json" \
    "${FIELD_DIR}/generation-report.json" > "${TMP_DIR}/source-summary.json"

jq -s \
    '{schema: "cubey.terrain.diffusion-seam-validation.v1",
      fields: map({seed, validation})}' \
    "${FIELD_DIR}"/fields/seed-*/manifest.json > "${TMP_DIR}/seam-validation.json"

jq -n \
    --arg commit "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    --arg generation_report_sha256 "$(sha256sum "${FIELD_DIR}/generation-report.json" | awk '{print $1}')" \
    --argjson lanes "$(printf '%s\n' "${lanes[@]}" | jq -R . | jq -s .)" \
    --argjson seeds "$(printf '%s\n' "${seeds[@]}" | jq -Rn '[inputs | tonumber]')" \
    --argjson azimuths "$(printf '%s\n' "${azimuths[@]}" | jq -Rn '[inputs | tonumber]')" \
    '{
      schema: "cubey.terrain.diffusion-bakeoff-capture.v1",
      cubey_commit: $commit,
      generation_report: "generated/generation-report.json",
      generation_report_sha256: $generation_report_sha256,
      repeat_validation: "repeat-validation.json",
      lanes: $lanes,
      seeds: $seeds,
      azimuth_degrees: $azimuths,
      render: {
        resolution: [1920, 1080],
        profile: "hard-cut-v1",
        visible_extent_m: [3200, 16384],
        orbit_radius_m: 100,
        orbit_elevation_degrees: 8,
        mesh_density: "high",
        render_stride: 1,
        weathering: "off",
        sun_elevation_degrees: 30,
        sun_azimuth_degrees: -55
      }
    }' > "${TMP_DIR}/capture-metadata.json"

cat > "${TMP_DIR}/REVIEW.md" <<'EOF'
# Terrain Diffusion Bakeoff v1

Review in this order:

1. `selection-contact-sheet.png` verifies that mountain windows were selected
   deterministically rather than by eye.
2. `height-contact-sheet.png` compares broad mass, range continuity, ridge
   body, summit hierarchy, valley coherence, and seed variation.
3. `slope-contact-sheet.png` exposes thin fins, cone clusters, repeated
   templates, shelves, and dominant grid or diagonal directions.
4. `seam-contact-sheet.png` magnifies the two quadrant joins; confirm its
   evidence against `seam-validation.json`.
5. `repeat-validation.json` records byte-level cross-process repeat evidence
   when a second generated field set is available.
6. The three clay sheets compare silhouettes through the common renderer at
   three yaws. Material cannot hide source morphology here.
7. `presentation-seed-9012.png` checks source/render compatibility last.
8. `climate-contact-sheet.png` checks preservation only. Climate does not
   influence this renderer.

Rows in the comparison sheets are frozen v2.1, graduated Mountains, and the
pinned Terrain Diffusion 30 m source. This pack records evidence; promotion is
decided separately in the terrain design notes.
EOF

rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
trap - EXIT
printf 'terrain diffusion bakeoff: wrote %s\n' "${OUT_DIR}"
