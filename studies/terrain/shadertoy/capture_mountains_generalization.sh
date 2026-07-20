#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${1:-${ROOT_DIR}/build/dev-terrain-studies/studies/terrain/shadertoy/terrain_shadertoy}"
OUT_DIR="${2:-${ROOT_DIR}/outputs/terrain_shadertoy_ref/mountains-generalization-v1}"
SOURCE="${CUBEY_SHADERTOY_MOUNTAINS_SOURCE:-${ROOT_DIR}/../ref/ShaderToy/mountains.glsl}"
TMP_DIR="${OUT_DIR}.tmp.$$"
trap 'rm -rf "${TMP_DIR}"' EXIT

if [[ ! -x "${APP}" ]]; then
    printf 'Mountains generalization capture: executable not found: %s\n' "${APP}" >&2
    exit 2
fi
if [[ ! -f "${SOURCE}" ]]; then
    printf 'Mountains generalization capture: source not found: %s\n' "${SOURCE}" >&2
    exit 2
fi
for command in awk git jq magick realpath sed sha256sum sort wc; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Mountains generalization capture: %s is required\n' "${command}" >&2
        exit 2
    fi
done

rm -rf "${TMP_DIR}"
mkdir -p "${TMP_DIR}/raw/yaw/original" "${TMP_DIR}/raw/yaw/clay" \
    "${TMP_DIR}/raw/simplification" "${TMP_DIR}/raw/performance" \
    "${TMP_DIR}/profiles" "${TMP_DIR}/profile-videos"

capture_scene() {
    local output="$1"
    shift
    "${APP}" --headless --width 1920 --height 1080 --output "${output}" "$@"
}

times=(0 20)
yaws=(0 45 90 135 180 225 270 315)
for time in "${times[@]}"; do
    for yaw in "${yaws[@]}"; do
        for shading in original clay; do
            capture_scene \
                "${TMP_DIR}/raw/yaw/${shading}/time-${time}-yaw-${yaw}.png" \
                --reference-render mesh --reference-time "${time}" \
                --reference-yaw-offset-deg "${yaw}" --reference-mesh-cells 1024 \
                --reference-mesh-surface map --reference-normal detailed \
                --reference-shading "${shading}"
        done
    done
done

simplification_specs=("0 180 far" "20 0 stress")
for spec in "${simplification_specs[@]}"; do
    read -r time yaw view <<<"${spec}"
    for cells in 256 512 1024; do
        for normal in geometry atlas detailed; do
            capture_scene \
                "${TMP_DIR}/raw/simplification/${view}-cells-${cells}-${normal}.png" \
                --reference-render mesh --reference-time "${time}" \
                --reference-yaw-offset-deg "${yaw}" --reference-mesh-cells "${cells}" \
                --reference-mesh-surface map --reference-normal "${normal}" \
                --reference-shading clay
        done
    done
done

for normal in atlas detailed; do
    capture_scene "${TMP_DIR}/raw/performance/cells-512-${normal}-original.png" \
        --reference-render mesh --reference-time 20 --reference-yaw-offset-deg 0 \
        --reference-mesh-cells 512 --reference-mesh-surface map \
        --reference-normal "${normal}" --reference-shading original
done

for shading in original clay; do
    yaw_inputs=()
    for time in "${times[@]}"; do
        for yaw in "${yaws[@]}"; do
            yaw_inputs+=(
                -label "time ${time} / yaw ${yaw}"
                "${TMP_DIR}/raw/yaw/${shading}/time-${time}-yaw-${yaw}.png"
            )
        done
    done
    magick montage "${yaw_inputs[@]}" -tile 8x2 -geometry 480x270+6+22 \
        "${TMP_DIR}/yaw-${shading}-contact-sheet.png"
done

simplification_inputs=()
for cells in 256 512 1024; do
    for view in far stress; do
        for normal in geometry atlas detailed; do
            simplification_inputs+=(
                -label "${view} / ${cells} / ${normal}"
                "${TMP_DIR}/raw/simplification/${view}-cells-${cells}-${normal}.png"
            )
        done
    done
done
magick montage "${simplification_inputs[@]}" -tile 6x3 -geometry 480x270+6+22 \
    "${TMP_DIR}/simplification-contact-sheet.png"

profile_entries="${TMP_DIR}/profile-entries.jsonl"
: > "${profile_entries}"

profile_config() {
    local name="$1"
    local cells="$2"
    local normal="$3"
    local shading="$4"
    local prefix="${TMP_DIR}/profiles/${name}"
    local video="${TMP_DIR}/profile-videos/${name}.mp4"
    local log="${TMP_DIR}/profiles/${name}.log"

    "${APP}" --headless --capture video --frames 90 --fps 30 \
        --width 2560 --height 1440 --output "${video}" \
        --profile-output "${prefix}" --profile-warmup-frames 30 \
        --reference-render mesh --reference-time 20 --reference-yaw-offset-deg 0 \
        --reference-mesh-cells "${cells}" --reference-mesh-surface map \
        --reference-normal "${normal}" --reference-shading "${shading}" \
        >"${log}" 2>&1
    rm -f "${video}"

    local durations="${TMP_DIR}/profiles/${name}-surface-durations.txt"
    awk -F, '$2 == "gpu" && $3 == "terrain_shadertoy_ref.surface" { print $5 }' \
        "${prefix}.passes.csv" | sort -n > "${durations}"
    local sample_count
    sample_count="$(wc -l < "${durations}" | tr -d ' ')"
    if (( sample_count < 50 )); then
        printf 'Mountains generalization capture: %s has only %s surface samples\n' \
            "${name}" "${sample_count}" >&2
        exit 1
    fi
    local p50_rank p95_rank p50_ms p95_ms triangles vertex_bytes index_bytes
    p50_rank="$(( (50 * sample_count + 99) / 100 ))"
    p95_rank="$(( (95 * sample_count + 99) / 100 ))"
    p50_ms="$(sed -n "${p50_rank}p" "${durations}")"
    p95_ms="$(sed -n "${p95_rank}p" "${durations}")"
    triangles="$(( 2 * cells * cells ))"
    vertex_bytes="$(( (cells + 1) * (cells + 1) * 8 ))"
    index_bytes="$(( cells * cells * 6 * 4 ))"
    jq -n \
        --arg name "${name}" --arg normal "${normal}" --arg shading "${shading}" \
        --argjson cells "${cells}" --argjson triangles "${triangles}" \
        --argjson vertex_bytes "${vertex_bytes}" --argjson index_bytes "${index_bytes}" \
        --argjson sample_count "${sample_count}" --argjson p50_ms "${p50_ms}" \
        --argjson p95_ms "${p95_ms}" \
        '{name: $name, cells: $cells, triangles: $triangles, normal: $normal,
          shading: $shading, mesh_bytes: {vertices: $vertex_bytes, indices: $index_bytes},
          gpu_surface: {samples: $sample_count, p50_ms: $p50_ms, p95_ms: $p95_ms,
            limit_ms: 1.0, timing_eligible: ($p95_ms <= 1.0)}}' >> "${profile_entries}"
    printf 'Mountains generalization profile: %-31s p95 %s ms\n' "${name}" "${p95_ms}"
}

profile_config cells-256-geometry-clay 256 geometry clay
profile_config cells-512-geometry-clay 512 geometry clay
profile_config cells-1024-geometry-clay 1024 geometry clay
profile_config cells-512-atlas-clay 512 atlas clay
profile_config cells-512-detailed-clay 512 detailed clay
profile_config cells-512-atlas-original 512 atlas original
profile_config cells-512-detailed-original 512 detailed original
profile_config cells-1024-detailed-original 1024 detailed original

jq -s '.' "${profile_entries}" > "${TMP_DIR}/performance-summary.json"

p95_for() {
    jq -r --arg name "$1" '.[] | select(.name == $name) | .gpu_surface.p95_ms' \
        "${TMP_DIR}/performance-summary.json"
}

performance_inputs=(
    -label "256 geometry clay / $(p95_for cells-256-geometry-clay) ms"
    "${TMP_DIR}/raw/simplification/stress-cells-256-geometry.png"
    -label "512 geometry clay / $(p95_for cells-512-geometry-clay) ms"
    "${TMP_DIR}/raw/simplification/stress-cells-512-geometry.png"
    -label "1024 geometry clay / $(p95_for cells-1024-geometry-clay) ms"
    "${TMP_DIR}/raw/simplification/stress-cells-1024-geometry.png"
    -label "512 atlas clay / $(p95_for cells-512-atlas-clay) ms"
    "${TMP_DIR}/raw/simplification/stress-cells-512-atlas.png"
    -label "512 detailed clay / $(p95_for cells-512-detailed-clay) ms"
    "${TMP_DIR}/raw/simplification/stress-cells-512-detailed.png"
    -label "512 atlas original / $(p95_for cells-512-atlas-original) ms"
    "${TMP_DIR}/raw/performance/cells-512-atlas-original.png"
    -label "512 detailed original / $(p95_for cells-512-detailed-original) ms"
    "${TMP_DIR}/raw/performance/cells-512-detailed-original.png"
    -label "1024 detailed original / $(p95_for cells-1024-detailed-original) ms"
    "${TMP_DIR}/raw/yaw/original/time-20-yaw-0.png"
)
magick montage "${performance_inputs[@]}" -tile 4x2 -geometry 480x270+6+22 \
    "${TMP_DIR}/performance-contact-sheet.png"

source_sha256="$(sha256sum "${SOURCE}" | awk '{print $1}')"
cubey_commit="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
source_path="$(realpath --relative-to "${ROOT_DIR}" "${SOURCE}")"
app_path="$(realpath --relative-to "${ROOT_DIR}" "${APP}")"
gpu_name="$(sed -n 's/^headless_video: \(.*\) wrote .*/\1/p' \
    "${TMP_DIR}/profiles/cells-256-geometry-clay.log" | head -1)"
jq -n \
    --arg source_path "${source_path}" --arg source_sha256 "${source_sha256}" \
    --arg cubey_commit "${cubey_commit}" --arg app "${app_path}" --arg gpu "${gpu_name}" \
    --slurpfile profiles "${TMP_DIR}/performance-summary.json" \
    '{
        schema: "cubey.terrain.shadertoy-mountains-generalization.v1",
        external_source: {
            path: $source_path,
            sha256: $source_sha256,
            url: "https://www.shadertoy.com/view/4slGD4",
            declared_license: "CC BY-NC-SA 3.0 Unported",
            vendored: false
        },
        cubey_commit: $cubey_commit,
        executable: $app,
        capture_script: "studies/terrain/shadertoy/capture_mountains_generalization.sh",
        input_substitutions: {
            iChannel0: "deterministic generated RGBA8 hash texture",
            iChannel1: "deterministic generated RGBA8 hash texture",
            iMouse: [0, 0, 0, 0]
        },
        yaw_review: {
            times_seconds: [0, 20],
            offsets_degrees: [0, 45, 90, 135, 180, 225, 270, 315],
            resolution: [1920, 1080],
            cells: 1024,
            normal: "detailed",
            shadings: ["original", "clay"]
        },
        simplification_review: {
            views: [{time: 0, yaw: 180, label: "far"},
                    {time: 20, yaw: 0, label: "stress"}],
            cells: [256, 512, 1024],
            normals: ["geometry", "atlas", "detailed"],
            shading: "clay"
        },
        resources: {
            height_atlas: {extent: [2048, 2048], format: "RGBA32F", bytes: 67108864},
            domain_extent_reference_units: 512
        },
        performance: {
            gpu: $gpu,
            resolution: [2560, 1440],
            frames: 90,
            warmup_frames: 30,
            surface_p95_limit_ms: 1.0,
            excluded: ["height-atlas bake", "sky", "host", "capture", "encoding"],
            configurations: $profiles[0]
        },
        generalization_gate: {
            required_credible_far_directions: 6,
            total_far_directions: 8,
            requires_original_and_clay: true,
            visual_result: "pending review"
        },
        sheets: ["yaw-original-contact-sheet.png", "yaw-clay-contact-sheet.png",
                 "simplification-contact-sheet.png", "performance-contact-sheet.png"]
    }' > "${TMP_DIR}/capture-metadata.json"

printf '%s\n' \
    'ShaderToy Mountains generalization v1' \
    '' \
    '1. Review yaw-original for presentation robustness across both source positions.' \
    '2. Review yaw-clay for source shape without material camouflage.' \
    '3. Use simplification to separate topology from atlas and exact normal detail.' \
    '4. Use performance last to compare the same visible tradeoffs against the 1 ms gate.' \
    '' \
    'The external source and generated SPIR-V remain outside this evidence pack.' \
    > "${TMP_DIR}/REVIEW.txt"

rm -rf "${TMP_DIR}/profile-videos" "${TMP_DIR}/profile-entries.jsonl"
rm -rf "${OUT_DIR}"
mv "${TMP_DIR}" "${OUT_DIR}"
printf 'Mountains generalization capture: wrote %s\n' "${OUT_DIR}"
