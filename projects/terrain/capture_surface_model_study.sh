#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/build/dev/assets/terrain/default}"
SURFACE_FIELDS="${SURFACE_FIELDS:-${ROOT_DIR}/build/dev/assets/terrain/surface-study}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/surface-model-study-v1}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-150}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"

MODELS=(mineral-control landform-transition climate-transition)

metric_last() {
    local metrics="$1"
    local category="$2"
    local name="$3"
    awk -F, -v category="${category}" -v name="${name}" \
        '$2 == category && $3 == name { value = $4 }
         END { if (value == "") value = 0; printf "%.6f", value }' "${metrics}"
}

metric_hash() {
    local metrics="$1"
    local prefix="$2"
    local low high
    low="$(metric_last "${metrics}" terrain.backdrop "${prefix}_low32")"
    high="$(metric_last "${metrics}" terrain.backdrop "${prefix}_high32")"
    printf '0x%08x%08x' "${high%%.*}" "${low%%.*}"
}

aggregate_gpu_values() {
    local passes="$1"
    local output="$2"
    awk -F, '
        NR > 1 && $2 == "gpu" {
            include = $3 == "terrain shadow" || $3 == "terrain surface" ||
                      $3 == "terrain stage proxy" || $3 == "terrain atmosphere" ||
                      $3 == "terrain post"
            if (include) total[$1] += $5
        }
        END { for (frame in total) printf "%.9f\n", total[frame] }
    ' "${passes}" | sort -n >"${output}"
}

value_stats() {
    local values="$1"
    awk '
        { value[NR] = $1; sum += $1 }
        END {
            if (NR == 0) {
                printf "0.000000\t0.000000\t0.000000\n"
                exit
            }
            if (NR % 2 == 0) median = (value[NR / 2] + value[NR / 2 + 1]) * 0.5
            else median = value[(NR + 1) / 2]
            p95_index = int(NR * 0.95)
            if (p95_index < NR * 0.95) ++p95_index
            if (p95_index < 1) p95_index = 1
            printf "%.6f\t%.6f\t%.6f\n", sum / NR, median, value[p95_index]
        }
    ' "${values}"
}

manifest_path() {
    local path="$1"
    local filename="$2"
    if [[ -d "${path}" ]]; then
        printf '%s/%s' "${path}" "${filename}"
    else
        printf '%s' "${path}"
    fi
}

if [[ ! -x "${APP}" ]]; then
    printf 'terrain executable not found: %s\n' "${APP}" >&2
    exit 1
fi

HEIGHT_MANIFEST="$(manifest_path "${HEIGHTFIELD}" heightfield.json)"
CLIMATE_MANIFEST="$(manifest_path "${SURFACE_FIELDS}" surface-fields.json)"
for manifest in "${HEIGHT_MANIFEST}" "${CLIMATE_MANIFEST}"; do
    if [[ ! -f "${manifest}" ]]; then
        printf 'terrain study manifest not found: %s\n' "${manifest}" >&2
        exit 1
    fi
done

mkdir -p "${OUT_DIR}"
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +

COMMON_ARGS=(
    --terrain-heightfield "${HEIGHTFIELD}"
    --terrain-surface-fields "${SURFACE_FIELDS}"
    --terrain-camera-preset backdrop
    --terrain-render-stride 3
    --terrain-surface-detail filtered-detail
    --terrain-shadows
    --time-of-day-mode solar
    --time-hours 9
    --day-of-year 172
    --latitude-degrees 35
    --pause-time
    --no-clouds
)

capture() {
    local model="$1"
    local name="$2"
    local title="$3"
    local group="$4"
    shift 4
    local lane="${OUT_DIR}/${model}"
    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        "${COMMON_ARGS[@]}" --terrain-surface-model "${model}" "$@" \
        --output "${lane}/${name}.png"
    local args="$*"
    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" \
        >>"${lane}/manifest.tsv"
}

for model in "${MODELS[@]}"; do
    lane="${OUT_DIR}/${model}"
    mkdir -p "${lane}/profiles"
    printf 'file\ttitle\tgroup\targs\n' >"${lane}/manifest.tsv"

    for heading in 0 90 180 270; do
        capture "${model}" "qualified-${heading}" "${model}: ${heading} deg" qualified \
            --terrain-placement selected --terrain-foreground-height 500 \
            --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
            --terrain-backdrop-azimuth "${heading}"
    done
    for heading in 90 180; do
        capture "${model}" "raking-${heading}" "${model}: raking ${heading}" raking \
            --terrain-placement selected --terrain-foreground-height 500 \
            --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
            --terrain-backdrop-azimuth "${heading}" --time-of-day-mode manual \
            --sun-elevation 12 --sun-azimuth 35
        capture "${model}" "stress-${heading}" "${model}: 100 m ${heading}" stress \
            --terrain-placement selected --terrain-foreground-height 100 \
            --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
            --terrain-backdrop-azimuth "${heading}"
    done

    capture "${model}" raw-center "${model}: raw center" placement \
        --terrain-placement raw-center --terrain-foreground-height 500 \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth 90
    for index in 0 1 2; do
        capture "${model}" "raw-sample-${index}" "${model}: raw sample ${index}" placement \
            --terrain-placement raw-sample --terrain-placement-index "${index}" \
            --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
            --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth 90
    done

    for diagnostic in vegetation moisture material-weights material-albedo; do
        capture "${model}" "diagnostic-${diagnostic}" "${model}: ${diagnostic}" diagnostic \
            --terrain-placement selected --terrain-foreground-height 500 \
            --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
            --terrain-backdrop-azimuth 90 --debug-view "${diagnostic}"
    done

    profile_prefix="${lane}/profiles/clear-stride3"
    profile_video="${lane}/profiles/clear-stride3.mp4"
    "${APP}" --headless --capture video --frames "${FRAMES}" --fps "${FPS}" \
        --width "${WIDTH}" --height "${HEIGHT}" "${COMMON_ARGS[@]}" \
        --terrain-surface-model "${model}" --terrain-placement selected \
        --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
        --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth 90 \
        --profile-output "${profile_prefix}" --profile-warmup-frames "${WARMUP_FRAMES}" \
        --output "${profile_video}"
    rm -f "${profile_video}"

    values="${lane}/profiles/clear-values.tmp"
    aggregate_gpu_values "${profile_prefix}.passes.csv" "${values}"
    IFS=$'\t' read -r mean p50 p95 < <(value_stats "${values}")
    rm -f "${values}"
    printf 'model\tmean_ms\tp50_ms\tp95_ms\n%s\t%s\t%s\t%s\n' \
        "${model}" "${mean}" "${p50}" "${p95}" >"${lane}/profile-summary.tsv"

    metrics="${profile_prefix}.metrics.csv"
    content_hash="$(metric_hash "${metrics}" content_hash)"
    geometry_hash="$(metric_hash "${metrics}" geometry_hash)"
    triangles="$(metric_last "${metrics}" terrain.backdrop product_render_triangles)"
    samples="$(metric_last "${metrics}" terrain.backdrop source_samples)"
    stride="$(metric_last "${metrics}" terrain.backdrop render_stride)"
    focus_x="$(metric_last "${metrics}" terrain.placement source_focus_x_m)"
    focus_z="$(metric_last "${metrics}" terrain.placement source_focus_z_m)"
    vegetation="$(metric_last "${metrics}" terrain.surface mean_vegetation)"
    moisture="$(metric_last "${metrics}" terrain.surface mean_moisture)"

    jq -n \
        --arg schema cubey.terrain.surface-model-study.v1 \
        --arg model "${model}" \
        --arg git_revision "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
        --arg heightfield_manifest "${HEIGHT_MANIFEST}" \
        --arg surface_fields_manifest "${CLIMATE_MANIFEST}" \
        --arg elevation_sha256 "$(jq -r '.files.elevation.sha256' "${HEIGHT_MANIFEST}")" \
        --arg climate_sha256 "$(jq -r '.files.climate.sha256' "${CLIMATE_MANIFEST}")" \
        --arg product_content_hash "${content_hash}" \
        --arg geometry_hash "${geometry_hash}" \
        --argjson triangles "${triangles%%.*}" \
        --argjson source_samples "${samples%%.*}" \
        --argjson render_stride "${stride%%.*}" \
        --argjson source_focus_x_m "${focus_x}" \
        --argjson source_focus_z_m "${focus_z}" \
        --argjson mean_vegetation "${vegetation}" \
        --argjson mean_moisture "${moisture}" \
        --argjson mean_ms "${mean}" \
        --argjson p50_ms "${p50}" \
        --argjson p95_ms "${p95}" \
        --argjson width "${WIDTH}" \
        --argjson height "${HEIGHT}" \
        '{
            schema: $schema,
            model: $model,
            git_revision: $git_revision,
            source: {
                heightfield_manifest: $heightfield_manifest,
                surface_fields_manifest: $surface_fields_manifest,
                elevation_sha256: $elevation_sha256,
                climate_sha256: $climate_sha256
            },
            product: {
                content_hash: $product_content_hash,
                geometry_hash: $geometry_hash,
                render_triangles: $triangles,
                source_samples: $source_samples,
                render_stride: $render_stride,
                source_focus_x_m: $source_focus_x_m,
                source_focus_z_m: $source_focus_z_m,
                mean_vegetation: $mean_vegetation,
                mean_moisture: $mean_moisture
            },
            profile: {mean_ms: $mean_ms, p50_ms: $p50_ms, p95_ms: $p95_ms},
            capture: {width: $width, height: $height}
        }' >"${lane}/review-metadata.json"
done

frozen_filter='{
    elevation_sha256: .source.elevation_sha256,
    climate_sha256: .source.climate_sha256,
    geometry_hash: .product.geometry_hash,
    render_triangles: .product.render_triangles,
    source_samples: .product.source_samples,
    render_stride: .product.render_stride,
    source_focus_x_m: .product.source_focus_x_m,
    source_focus_z_m: .product.source_focus_z_m
}'
control_frozen="$(jq -c "${frozen_filter}" "${OUT_DIR}/mineral-control/review-metadata.json")"
for model in landform-transition climate-transition; do
    candidate_frozen="$(jq -c "${frozen_filter}" "${OUT_DIR}/${model}/review-metadata.json")"
    if [[ "${candidate_frozen}" != "${control_frozen}" ]]; then
        printf 'surface-model frozen metadata mismatch for %s\ncontrol: %s\ncandidate: %s\n' \
            "${model}" "${control_frozen}" "${candidate_frozen}" >&2
        exit 1
    fi
done

control_mean="$(jq -r '.profile.mean_ms' "${OUT_DIR}/mineral-control/review-metadata.json")"
control_p50="$(jq -r '.profile.p50_ms' "${OUT_DIR}/mineral-control/review-metadata.json")"
for model in landform-transition climate-transition; do
    candidate_mean="$(jq -r '.profile.mean_ms' "${OUT_DIR}/${model}/review-metadata.json")"
    candidate_p50="$(jq -r '.profile.p50_ms' "${OUT_DIR}/${model}/review-metadata.json")"
    if ! awk -v cm="${control_mean}" -v cp="${control_p50}" \
        -v nm="${candidate_mean}" -v np="${candidate_p50}" '
        BEGIN {
            pass = nm <= 1.10 && np <= 1.10 && nm <= cm + 0.10 && np <= cp + 0.10
            exit pass ? 0 : 1
        }'; then
        printf '%s profile gate failed: control %.6f / %.6f ms, candidate %.6f / %.6f ms\n' \
            "${model}" "${control_mean}" "${control_p50}" \
            "${candidate_mean}" "${candidate_p50}" >&2
        exit 1
    fi
done

if command -v magick >/dev/null 2>&1; then
    montage_group() {
        local output="$1"
        shift
        local inputs=()
        local name model
        for name in "$@"; do
            for model in "${MODELS[@]}"; do
                inputs+=(-label "${model}: ${name}" "${OUT_DIR}/${model}/${name}.png")
            done
        done
        magick montage "${inputs[@]}" -geometry 400x225+8+26 -tile 3x \
            "${OUT_DIR}/${output}"
    }
    montage_group qualified-comparison.png qualified-0 qualified-90 qualified-180 qualified-270
    montage_group raking-comparison.png raking-90 raking-180
    montage_group stress-comparison.png stress-90 stress-180
    montage_group placement-comparison.png raw-center raw-sample-0 raw-sample-1 raw-sample-2
    montage_group diagnostic-comparison.png diagnostic-vegetation diagnostic-moisture \
        diagnostic-material-weights diagnostic-material-albedo
fi

printf 'surface-model study: wrote %s\n' "${OUT_DIR}"
