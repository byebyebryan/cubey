#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/build/dev/assets/terrain/default}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/lighting-material-v1}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-120}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"

if [[ ! -x "${APP}" ]]; then
    printf 'terrain executable not found: %s\n' "${APP}" >&2
    printf 'Build it with: cmake --build --preset dev --target cubey_project_terrain\n' >&2
    exit 1
fi
if [[ ! -f "${HEIGHTFIELD}/heightfield.json" && ! -f "${HEIGHTFIELD}" ]]; then
    printf 'terrain heightfield not found: %s\n' "${HEIGHTFIELD}" >&2
    printf 'Generate it with: cmake --build --preset dev --target cubey_terrain_generate_default_asset\n' >&2
    exit 1
fi

mkdir -p "${OUT_DIR}/profiles"
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 ! -name profiles -exec rm -rf {} +
find "${OUT_DIR}/profiles" -mindepth 1 -maxdepth 1 -delete

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
PROFILE_SUMMARY="${OUT_DIR}/profile-summary.tsv"
ALL_FILES=()
ALL_LABELS=()
LIGHTING_FILES=()
LIGHTING_LABELS=()
FLAT_FILES=()
FLAT_LABELS=()
REFINED_FILES=()
REFINED_LABELS=()
ALBEDO_FILES=()
ALBEDO_LABELS=()
NORMAL_FILES=()
NORMAL_LABELS=()
CONTROL_FILES=()
CONTROL_LABELS=()

printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"

capture() {
    local name="$1"
    local title="$2"
    local group="$3"
    shift 3

    "${APP}" \
        --headless \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --terrain-heightfield "${HEIGHTFIELD}" \
        --terrain-placement selected \
        --terrain-camera-preset backdrop \
        --terrain-foreground-height 100 \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="$*"
    args="${args//$'\t'/ }"
    ALL_FILES+=("${OUT_DIR}/${name}.png")
    ALL_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" \
        >>"${MANIFEST}"
}

capture lighting-neutral-off "Neutral: shadows off" lighting \
    --no-terrain-shadows --terrain-surface-detail filtered-detail
LIGHTING_FILES+=("${OUT_DIR}/lighting-neutral-off.png")
LIGHTING_LABELS+=("Neutral: shadows off")
capture lighting-neutral-on "Neutral: shadows on" lighting \
    --terrain-shadows --terrain-surface-detail filtered-detail
LIGHTING_FILES+=("${OUT_DIR}/lighting-neutral-on.png")
LIGHTING_LABELS+=("Neutral: shadows on")
capture lighting-neutral-visibility "Neutral: sun visibility" lighting \
    --terrain-shadows --terrain-surface-detail flat --debug-view sun-visibility
LIGHTING_FILES+=("${OUT_DIR}/lighting-neutral-visibility.png")
LIGHTING_LABELS+=("Neutral: sun visibility")

capture lighting-raking-off "Raking: shadows off" lighting \
    --no-terrain-shadows --terrain-surface-detail filtered-detail \
    --sun-elevation 12 --sun-azimuth 35
LIGHTING_FILES+=("${OUT_DIR}/lighting-raking-off.png")
LIGHTING_LABELS+=("Raking: shadows off")
capture lighting-raking-on "Raking: shadows on" lighting \
    --terrain-shadows --terrain-surface-detail filtered-detail \
    --sun-elevation 12 --sun-azimuth 35
LIGHTING_FILES+=("${OUT_DIR}/lighting-raking-on.png")
LIGHTING_LABELS+=("Raking: shadows on")
capture lighting-raking-visibility "Raking: sun visibility" lighting \
    --terrain-shadows --terrain-surface-detail flat --debug-view sun-visibility \
    --sun-elevation 12 --sun-azimuth 35
LIGHTING_FILES+=("${OUT_DIR}/lighting-raking-visibility.png")
LIGHTING_LABELS+=("Raking: sun visibility")

for heading in 0 90 180 270; do
    capture "heading-flat-${heading}" "Flat, ${heading} deg" headings \
        --terrain-shadows --terrain-surface-detail flat \
        --terrain-backdrop-azimuth "${heading}"
    FLAT_FILES+=("${OUT_DIR}/heading-flat-${heading}.png")
    FLAT_LABELS+=("Flat, ${heading} deg")
done

for heading in 0 90 180 270; do
    capture "heading-refined-${heading}" "Refined, ${heading} deg" headings \
        --terrain-shadows --terrain-surface-detail filtered-detail \
        --terrain-backdrop-azimuth "${heading}"
    REFINED_FILES+=("${OUT_DIR}/heading-refined-${heading}.png")
    REFINED_LABELS+=("Refined, ${heading} deg")
done

for heading in 0 90 180 270; do
    capture "heading-albedo-${heading}" "Albedo, ${heading} deg" material-diagnostics \
        --terrain-shadows --terrain-surface-detail filtered-detail \
        --terrain-backdrop-azimuth "${heading}" --debug-view material-albedo
    ALBEDO_FILES+=("${OUT_DIR}/heading-albedo-${heading}.png")
    ALBEDO_LABELS+=("Albedo, ${heading} deg")
done

for heading in 0 90 180 270; do
    capture "heading-normal-${heading}" "Normal, ${heading} deg" material-diagnostics \
        --terrain-shadows --terrain-surface-detail filtered-detail \
        --terrain-backdrop-azimuth "${heading}" --debug-view material-normal
    NORMAL_FILES+=("${OUT_DIR}/heading-normal-${heading}.png")
    NORMAL_LABELS+=("Normal, ${heading} deg")
done

capture control-100m "Foreground control: 100 m" controls \
    --terrain-camera-preset backdrop-stage --terrain-shadows \
    --terrain-surface-detail filtered-detail --terrain-foreground-height 100
CONTROL_FILES+=("${OUT_DIR}/control-100m.png")
CONTROL_LABELS+=("Foreground control: 100 m")
capture control-500m "Foreground control: 500 m" controls \
    --terrain-camera-preset backdrop-stage --terrain-shadows \
    --terrain-surface-detail filtered-detail --terrain-foreground-height 500
CONTROL_FILES+=("${OUT_DIR}/control-500m.png")
CONTROL_LABELS+=("Foreground control: 500 m")

for diagnostic in height slope classification-normal projected-edge; do
    capture "geometry-${diagnostic}" "Geometry: ${diagnostic}" geometry \
        --terrain-shadows --terrain-surface-detail flat --debug-view "${diagnostic}"
    CONTROL_FILES+=("${OUT_DIR}/geometry-${diagnostic}.png")
    CONTROL_LABELS+=("Geometry: ${diagnostic}")
done

profile_lane() {
    local lane="$1"
    shift
    local prefix="${OUT_DIR}/profiles/${lane}"
    local video="${OUT_DIR}/profiles/${lane}.mp4"

    external_gpu_busy() {
        command -v nvidia-smi >/dev/null 2>&1 || return 1
        nvidia-smi pmon -c 1 -s u 2>/dev/null | awk '
            $1 ~ /^[0-9]+$/ && $3 ~ /C/ && $4 ~ /^[0-9]+$/ && $4 + 0 >= 10 {
                busy = 1
            }
            END { exit busy ? 0 : 1 }
        '
    }

    wait_for_gpu_idle() {
        local idle_samples=0
        for _ in $(seq 1 60); do
            if external_gpu_busy; then
                idle_samples=0
            else
                idle_samples=$((idle_samples + 1))
                if (( idle_samples >= 2 )); then
                    return 0
                fi
            fi
            sleep 2
        done
        return 1
    }

    profile_lane_within_budget() {
        local summary="${prefix}.summary.txt"
        local atmosphere shadow terrain post combined
        atmosphere="$(awk -F, '$1 == "gpu" && $2 == "terrain atmosphere" {print $6; exit}' \
            "${summary}")"
        shadow="$(awk -F, '$1 == "gpu" && $2 == "terrain shadow" {print $6; exit}' \
            "${summary}")"
        terrain="$(awk -F, '$1 == "gpu" && $2 == "terrain surface" {print $6; exit}' \
            "${summary}")"
        post="$(awk -F, '$1 == "gpu" && $2 == "terrain post" {print $6; exit}' \
            "${summary}")"
        shadow="${shadow:-0}"
        combined="$(awk -v a="${atmosphere}" -v s="${shadow}" -v t="${terrain}" -v p="${post}" \
            'BEGIN { print a + s + t + p }')"
        awk -v combined="${combined}" -v shadow="${shadow}" -v forced="${lane}" '
            BEGIN {
                pass = combined <= 1.10
                if (forced == "shadow-forced-update") pass = pass && shadow < 0.50
                exit pass ? 0 : 1
            }
        '
    }

    for attempt in 1 2 3; do
        if ! wait_for_gpu_idle; then
            printf 'GPU remained busy before profile lane %s\n' "${lane}" >&2
            return 1
        fi
        "${APP}" \
            --headless \
            --capture video \
            --frames "${FRAMES}" \
            --fps "${FPS}" \
            --width "${WIDTH}" \
            --height "${HEIGHT}" \
            --terrain-heightfield "${HEIGHTFIELD}" \
            --terrain-placement selected \
            --terrain-camera-preset backdrop \
            --terrain-foreground-height 100 \
            --profile-output "${prefix}" \
            --profile-warmup-frames "${WARMUP_FRAMES}" \
            "$@" \
            --output "${video}"
        rm -f "${video}"
        if profile_lane_within_budget || ! external_gpu_busy; then
            return 0
        fi
        printf 'External GPU compute overlapped profile lane %s; retrying (%u/3)\n' \
            "${lane}" "${attempt}" >&2
    done
    printf 'Unable to capture uncontended profile lane %s\n' "${lane}" >&2
    return 1
}

profile_lane shadow-off-flat --no-terrain-shadows --terrain-surface-detail flat
profile_lane shadow-on-flat --terrain-shadows --terrain-surface-detail flat
profile_lane shadow-on-filtered --terrain-shadows --terrain-surface-detail filtered-detail
profile_lane shadow-forced-update --terrain-shadows --terrain-surface-detail flat \
    --time-of-day-mode solar --time-hours 10 --day-of-year 172 --latitude-degrees 35 \
    --time-speed-hours-per-second 2

span_median() {
    local summary="$1"
    local label="$2"
    awk -F, -v label="${label}" \
        '$1 == "gpu" && $2 == label { printf "%.6f", $6; found = 1; exit }
         END { if (!found) printf "0.000000" }' "${summary}"
}

metric_last() {
    local metrics="$1"
    local category="$2"
    local name="$3"
    awk -F, -v category="${category}" -v name="${name}" \
        '$2 == category && $3 == name { value = $4 }
         END { if (value == "") value = 0; printf "%.6f", value }' "${metrics}"
}

printf 'lane\tatmosphere_p50_ms\tshadow_p50_ms\tterrain_p50_ms\tpost_p50_ms\tcombined_p50_ms\tshadow_updates\n' \
    >"${PROFILE_SUMMARY}"
for lane in shadow-off-flat shadow-on-flat shadow-on-filtered shadow-forced-update; do
    summary="${OUT_DIR}/profiles/${lane}.summary.txt"
    metrics="${OUT_DIR}/profiles/${lane}.metrics.csv"
    atmosphere="$(span_median "${summary}" "terrain atmosphere")"
    shadow="$(span_median "${summary}" "terrain shadow")"
    terrain="$(span_median "${summary}" "terrain surface")"
    post="$(span_median "${summary}" "terrain post")"
    combined="$(awk -v a="${atmosphere}" -v s="${shadow}" -v t="${terrain}" -v p="${post}" \
        'BEGIN { printf "%.6f", a + s + t + p }')"
    updates="$(metric_last "${metrics}" "terrain.shadow" "update_count")"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${lane}" "${atmosphere}" "${shadow}" "${terrain}" "${post}" "${combined}" \
        "${updates}" >>"${PROFILE_SUMMARY}"
done

if command -v magick >/dev/null 2>&1; then
    lighting_inputs=()
    for index in "${!LIGHTING_FILES[@]}"; do
        lighting_inputs+=("-label" "${LIGHTING_LABELS[${index}]}" "${LIGHTING_FILES[${index}]}")
    done
    magick montage "${lighting_inputs[@]}" -geometry 480x270+8+26 -tile 3x2 \
        "${OUT_DIR}/lighting-contact-sheet.png"

    heading_inputs=()
    for index in "${!FLAT_FILES[@]}"; do
        heading_inputs+=("-label" "${FLAT_LABELS[${index}]}" "${FLAT_FILES[${index}]}")
    done
    for index in "${!REFINED_FILES[@]}"; do
        heading_inputs+=("-label" "${REFINED_LABELS[${index}]}" "${REFINED_FILES[${index}]}")
    done
    magick montage "${heading_inputs[@]}" -geometry 400x225+8+26 -tile 4x2 \
        "${OUT_DIR}/heading-contact-sheet.png"

    material_inputs=()
    for index in "${!ALBEDO_FILES[@]}"; do
        material_inputs+=("-label" "${ALBEDO_LABELS[${index}]}" "${ALBEDO_FILES[${index}]}")
    done
    for index in "${!NORMAL_FILES[@]}"; do
        material_inputs+=("-label" "${NORMAL_LABELS[${index}]}" "${NORMAL_FILES[${index}]}")
    done
    magick montage "${material_inputs[@]}" -geometry 400x225+8+26 -tile 4x2 \
        "${OUT_DIR}/material-contact-sheet.png"

    control_inputs=()
    for index in "${!CONTROL_FILES[@]}"; do
        control_inputs+=("-label" "${CONTROL_LABELS[${index}]}" "${CONTROL_FILES[${index}]}")
    done
    magick montage "${control_inputs[@]}" -geometry 480x270+8+26 -tile 2x3 \
        "${OUT_DIR}/control-contact-sheet.png"
fi

MANIFEST_PATH="${HEIGHTFIELD}"
if [[ -d "${HEIGHTFIELD}" ]]; then
    MANIFEST_PATH="${HEIGHTFIELD}/heightfield.json"
fi
GIT_REVISION="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
PROFILE_METRICS="${OUT_DIR}/profiles/shadow-on-filtered.metrics.csv"
HASH_LOW="$(metric_last "${PROFILE_METRICS}" "terrain.backdrop" "content_hash_low32")"
HASH_HIGH="$(metric_last "${PROFILE_METRICS}" "terrain.backdrop" "content_hash_high32")"
printf -v CONTENT_HASH '0x%08x%08x' "${HASH_HIGH%%.*}" "${HASH_LOW%%.*}"
PRODUCT_TRIANGLES="$(metric_last "${PROFILE_METRICS}" "terrain.backdrop" "product_render_triangles")"
SOURCE_SAMPLES="$(metric_last "${PROFILE_METRICS}" "terrain.backdrop" "source_samples")"
MATERIAL_BYTES="$(metric_last "${PROFILE_METRICS}" "terrain.backdrop" "material_texture_bytes")"
SHADOW_TRIANGLES="$(metric_last "${PROFILE_METRICS}" "terrain.shadow" "triangle_count")"
SHADOW_MAP_EXTENT="$(metric_last "${PROFILE_METRICS}" "terrain.shadow" "map_extent")"

jq -n \
    --arg schema "cubey.terrain.lighting-material-review.v1" \
    --arg git_revision "${GIT_REVISION}" \
    --arg executable "${APP}" \
    --arg heightfield_manifest "${MANIFEST_PATH}" \
    --arg elevation_sha256 "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")" \
    --arg product_content_hash "${CONTENT_HASH}" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" \
    --argjson capture_count "${#ALL_FILES[@]}" \
    --argjson product_triangles "${PRODUCT_TRIANGLES%%.*}" \
    --argjson source_samples "${SOURCE_SAMPLES%%.*}" \
    --argjson material_texture_bytes "${MATERIAL_BYTES%%.*}" \
    --arg shadow_caster_scope "outer-backdrop-sectors" \
    --argjson shadow_caster_triangles "${SHADOW_TRIANGLES%%.*}" \
    --argjson shadow_map_extent "${SHADOW_MAP_EXTENT%%.*}" \
    --argjson profile_frames "${FRAMES}" \
    --argjson profile_warmup_frames "${WARMUP_FRAMES}" \
    '{
        schema: $schema,
        git_revision: $git_revision,
        executable: $executable,
        heightfield_manifest: $heightfield_manifest,
        elevation_sha256: $elevation_sha256,
        product_content_hash: $product_content_hash,
        product_render_triangles: $product_triangles,
        source_samples: $source_samples,
        material_texture_bytes: $material_texture_bytes,
        shadow_caster_scope: $shadow_caster_scope,
        shadow_caster_triangles: $shadow_caster_triangles,
        shadow_map_extent: $shadow_map_extent,
        resolution: {width: $width, height: $height},
        profile: {frames: $profile_frames, warmup_frames: $profile_warmup_frames},
        capture_count: $capture_count
    }' >"${OUT_DIR}/review-metadata.json"

{
    printf '# Terrain Lighting And Material V1 Review\n\n'
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Runtime revision: `%s`\n' "${GIT_REVISION}"
    printf -- '- Heightfield SHA-256: `%s`\n' "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")"
    printf -- '- Product content hash: `%s`\n' "${CONTENT_HASH}"
    printf -- '- Geometry: %s render triangles from %s cached source samples\n' \
        "${PRODUCT_TRIANGLES%%.*}" "${SOURCE_SAMPLES%%.*}"
    printf -- '- Material allocation: %s bytes\n' "${MATERIAL_BYTES%%.*}"
    printf -- '- Shadow casters: %s outer-backdrop triangles; map: %s x %s\n\n' \
        "${SHADOW_TRIANGLES%%.*}" "${SHADOW_MAP_EXTENT%%.*}" "${SHADOW_MAP_EXTENT%%.*}"
    printf 'Start with the four contact sheets. Lighting isolates shadow response; headings '
    printf 'compare the frozen flat control with refined material; material diagnostics expose '
    printf 'tiling or noise; controls retain camera and geometry evidence.\n\n'
    printf '```tsv\n'
    cat "${PROFILE_SUMMARY}"
    printf '```\n\n'
    printf '| Capture | Group | Arguments |\n'
    printf '|---|---|---|\n'
    tail -n +2 "${MANIFEST}" | while IFS=$'\t' read -r file title group args; do
        printf '| [%s](%s) | %s | `%s` |\n' "${title}" "${file}" "${group}" "${args}"
    done
} >"${INDEX}"

printf 'Terrain lighting/material review written to %s\n' "${OUT_DIR}"
