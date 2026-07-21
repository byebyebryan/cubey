#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/build/dev/assets/terrain/default}"
MODE="${1:-}"
OUT_ROOT="${2:-${ROOT_DIR}/outputs/terrain/material-v2}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-150}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"

usage() {
    printf 'Usage: %s <control|candidate|finalize> [output-directory]\n' "$0" >&2
}

if [[ "${MODE}" != "control" && "${MODE}" != "candidate" && \
      "${MODE}" != "finalize" ]]; then
    usage
    exit 2
fi

metric_last() {
    local metrics="$1"
    local category="$2"
    local name="$3"
    awk -F, -v category="${category}" -v name="${name}" \
        '$2 == category && $3 == name { value = $4 }
         END { if (value == "") value = 0; printf "%.6f", value }' "${metrics}"
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

montage_group() {
    local output="$1"
    local tile="$2"
    local files_name="$3"
    local labels_name="$4"
    local -n files="${files_name}"
    local -n labels="${labels_name}"
    local inputs=()
    for index in "${!files[@]}"; do
        inputs+=("-label" "${labels[${index}]}" "${files[${index}]}")
    done
    magick montage "${inputs[@]}" -geometry 400x225+8+26 -tile "${tile}" "${output}"
}

if [[ "${MODE}" == "finalize" ]]; then
    if ! command -v magick >/dev/null 2>&1; then
        printf 'ImageMagick is required to finalize the matched review\n' >&2
        exit 1
    fi
    CONTROL_DIR="${OUT_ROOT}/control"
    CANDIDATE_DIR="${OUT_ROOT}/candidate"
    for lane_dir in "${CONTROL_DIR}" "${CANDIDATE_DIR}"; do
        if [[ ! -f "${lane_dir}/review-metadata.json" ]]; then
            printf 'missing review lane: %s\n' "${lane_dir}" >&2
            exit 1
        fi
    done

    frozen_fields='{
        elevation_sha256,
        product_content_hash,
        product_render_triangles,
        source_samples,
        render_stride,
        material_texture_bytes
    }'
    control_frozen="$(jq -c "${frozen_fields}" "${CONTROL_DIR}/review-metadata.json")"
    candidate_frozen="$(jq -c "${frozen_fields}" "${CANDIDATE_DIR}/review-metadata.json")"
    if [[ "${control_frozen}" != "${candidate_frozen}" ]]; then
        printf 'control/candidate frozen metadata mismatch\ncontrol:   %s\ncandidate: %s\n' \
            "${control_frozen}" "${candidate_frozen}" >&2
        exit 1
    fi

    read -r control_mean control_p50 control_p95 < <(
        tail -n 1 "${CONTROL_DIR}/profile-summary.tsv" | cut -f2-4
    )
    read -r candidate_mean candidate_p50 candidate_p95 < <(
        tail -n 1 "${CANDIDATE_DIR}/profile-summary.tsv" | cut -f2-4
    )
    if ! awk -v cm="${control_mean}" -v cp="${control_p50}" \
        -v nm="${candidate_mean}" -v np="${candidate_p50}" '
        BEGIN {
            pass = nm <= 1.10 && np <= 1.10 &&
                   nm <= cm + 0.10 && np <= cp + 0.10
            exit pass ? 0 : 1
        }'; then
        printf 'candidate profile gate failed: control %.6f / %.6f ms, candidate %.6f / %.6f ms\n' \
            "${control_mean}" "${control_p50}" "${candidate_mean}" "${candidate_p50}" >&2
        exit 1
    fi

    mkdir -p "${OUT_ROOT}"
    printf 'file\tae_pixels\tnormalized_rmse\n' >"${OUT_ROOT}/image-comparison.tsv"
    for image_name in \
        qualified-0 qualified-90 qualified-180 qualified-270 \
        raking-90 raking-180 stress-90 stress-180 \
        cloud-90 cloud-180 diagnostic-albedo-90 diagnostic-albedo-180 \
        diagnostic-normal-90 diagnostic-normal-180 \
        diagnostic-roughness-90 diagnostic-roughness-180 \
        diagnostic-weights-90 diagnostic-weights-180 flat-control; do
        control_image="${CONTROL_DIR}/${image_name}.png"
        candidate_image="${CANDIDATE_DIR}/${image_name}.png"
        ae="$(magick compare -metric AE "${control_image}" "${candidate_image}" null: 2>&1 || true)"
        rmse_output="$(magick compare -metric RMSE "${control_image}" "${candidate_image}" null: 2>&1 || true)"
        rmse="$(sed -n 's/.*(\([^)]*\)).*/\1/p' <<<"${rmse_output}")"
        printf '%s.png\t%s\t%s\n' "${image_name}" "${ae}" "${rmse:-0}" \
            >>"${OUT_ROOT}/image-comparison.tsv"
    done

    flat_ae="$(awk -F'\t' '$1 == "flat-control.png" {print $2}' \
        "${OUT_ROOT}/image-comparison.tsv")"
    if [[ "${flat_ae}" != "0" ]]; then
        printf 'flat control changed by %s pixels\n' "${flat_ae}" >&2
        exit 1
    fi

    if command -v magick >/dev/null 2>&1; then
        for group in qualified raking stress cloud; do
            case "${group}" in
            qualified) ids=(0 90 180 270) ;;
            *) ids=(90 180) ;;
            esac
            pair_files=()
            pair_labels=()
            for id in "${ids[@]}"; do
                pair_files+=("${CONTROL_DIR}/${group}-${id}.png"
                             "${CANDIDATE_DIR}/${group}-${id}.png")
                pair_labels+=("Control: ${group} ${id}" "Candidate: ${group} ${id}")
            done
            montage_group "${OUT_ROOT}/${group}-comparison.png" "2x${#ids[@]}" \
                pair_files pair_labels
        done

        diagnostic_files=()
        diagnostic_labels=()
        for diagnostic in albedo normal roughness weights; do
            for heading in 90 180; do
                diagnostic_files+=("${CONTROL_DIR}/diagnostic-${diagnostic}-${heading}.png"
                                   "${CANDIDATE_DIR}/diagnostic-${diagnostic}-${heading}.png")
                diagnostic_labels+=("Control: ${diagnostic} ${heading}"
                                    "Candidate: ${diagnostic} ${heading}")
            done
        done
        montage_group "${OUT_ROOT}/diagnostic-comparison.png" 4x4 \
            diagnostic_files diagnostic_labels
    fi

    {
        printf '# Terrain Material V2 Review\n\n'
        printf -- '- Frozen metadata: `%s`\n' "${control_frozen}"
        printf -- '- Control revision: `%s`\n' \
            "$(jq -r '.git_revision' "${CONTROL_DIR}/review-metadata.json")"
        printf -- '- Candidate revision: `%s`\n' \
            "$(jq -r '.git_revision' "${CANDIDATE_DIR}/review-metadata.json")"
        printf -- '- Flat-control changed pixels: `%s`\n\n' "${flat_ae}"
        printf '| Lane | Clear mean | Clear p50 | Clear p95 |\n'
        printf '|---|---:|---:|---:|\n'
        printf '| Control | %.6f ms | %.6f ms | %.6f ms |\n' \
            "${control_mean}" "${control_p50}" "${control_p95}"
        printf '| Candidate | %.6f ms | %.6f ms | %.6f ms |\n\n' \
            "${candidate_mean}" "${candidate_p50}" "${candidate_p95}"
        printf 'Review `qualified-comparison.png` first, then `raking-comparison.png` '
        printf 'and `diagnostic-comparison.png`. Stress and cloud sheets are supporting '
        printf 'evidence and do not expand the far-backdrop contract.\n'
    } >"${OUT_ROOT}/index.md"

    printf 'Terrain Material V2 comparison finalized at %s\n' "${OUT_ROOT}"
    exit 0
fi

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

OUT_DIR="${OUT_ROOT}/${MODE}"
PROFILE_DIR="${OUT_DIR}/profiles"
mkdir -p "${PROFILE_DIR}"
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 ! -name profiles -exec rm -rf {} +
find "${PROFILE_DIR}" -mindepth 1 -maxdepth 1 -delete

COMMON_ARGS=(
    --terrain-heightfield "${HEIGHTFIELD}"
    --terrain-placement selected
    --terrain-camera-preset backdrop
    --terrain-render-stride 3
    --terrain-surface-detail filtered-detail
    --terrain-shadows
    --time-of-day-mode solar
    --time-hours 9
    --day-of-year 172
    --latitude-degrees 35
    --pause-time
)

MANIFEST="${OUT_DIR}/manifest.tsv"
printf 'file\ttitle\tgroup\targs\n' >"${MANIFEST}"
ALL_FILES=()
ALL_LABELS=()

capture() {
    local name="$1"
    local title="$2"
    local group="$3"
    shift 3

    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        "${COMMON_ARGS[@]}" "$@" --output "${OUT_DIR}/${name}.png"
    local args="$*"
    args="${args//$'\t'/ }"
    ALL_FILES+=("${OUT_DIR}/${name}.png")
    ALL_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" \
        >>"${MANIFEST}"
}

for heading in 0 90 180 270; do
    capture "qualified-${heading}" "Qualified ${heading} deg" qualified \
        --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
        --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth "${heading}" \
        --no-clouds
done
for heading in 90 180; do
    capture "raking-${heading}" "Raking light ${heading} deg" raking \
        --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
        --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth "${heading}" \
        --time-of-day-mode manual --sun-elevation 12 --sun-azimuth 35 --no-clouds
    capture "stress-${heading}" "100 m stress ${heading} deg" stress \
        --terrain-foreground-height 100 --terrain-backdrop-orbit-radius 100 \
        --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth "${heading}" \
        --no-clouds
    capture "cloud-${heading}" "Fair cloud ${heading} deg" cloud \
        --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
        --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth "${heading}" \
        --clouds --cloud-weather-preset fair-weather
    for diagnostic in albedo normal roughness weights; do
        case "${diagnostic}" in
        albedo) debug_view=material-albedo ;;
        normal) debug_view=material-normal ;;
        roughness) debug_view=material-roughness ;;
        weights) debug_view=material-weights ;;
        esac
        capture "diagnostic-${diagnostic}-${heading}" \
            "${diagnostic} ${heading} deg" diagnostic \
            --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
            --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth "${heading}" \
            --debug-view "${debug_view}" --no-clouds
    done
done
capture flat-control "Flat invariant" invariant \
    --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
    --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth 90 \
    --terrain-surface-detail flat --no-clouds

external_gpu_busy() {
    command -v nvidia-smi >/dev/null 2>&1 || return 1
    nvidia-smi pmon -c 1 -s u 2>/dev/null | awk '
        $1 ~ /^[0-9]+$/ && $3 ~ /C/ && $4 ~ /^[0-9]+$/ && $4 + 0 >= 10 {
            busy = 1
        }
        END { exit busy ? 0 : 1 }
    '
}

idle_samples=0
for _ in $(seq 1 60); do
    if external_gpu_busy; then
        idle_samples=0
    else
        idle_samples=$((idle_samples + 1))
        if (( idle_samples >= 2 )); then
            break
        fi
    fi
    sleep 2
done
if (( idle_samples < 2 )); then
    printf 'GPU remained busy before profile lane %s\n' "${MODE}" >&2
    exit 1
fi

PROFILE_PREFIX="${PROFILE_DIR}/clear-stride3"
PROFILE_VIDEO="${PROFILE_DIR}/clear-stride3.mp4"
"${APP}" --headless --capture video --frames "${FRAMES}" --fps "${FPS}" \
    --width "${WIDTH}" --height "${HEIGHT}" "${COMMON_ARGS[@]}" \
    --terrain-foreground-height 500 --terrain-backdrop-orbit-radius 100 \
    --terrain-backdrop-elevation 8 --terrain-backdrop-azimuth 90 --no-clouds \
    --profile-output "${PROFILE_PREFIX}" \
    --profile-warmup-frames "${WARMUP_FRAMES}" --output "${PROFILE_VIDEO}"
rm -f "${PROFILE_VIDEO}"

VALUES="${PROFILE_DIR}/clear-values.tmp"
aggregate_gpu_values "${PROFILE_PREFIX}.passes.csv" "${VALUES}"
IFS=$'\t' read -r clear_mean clear_p50 clear_p95 < <(value_stats "${VALUES}")
rm -f "${VALUES}"
printf 'lane\tclear_mean_ms\tclear_p50_ms\tclear_p95_ms\n' \
    >"${OUT_DIR}/profile-summary.tsv"
printf '%s\t%s\t%s\t%s\n' "${MODE}" "${clear_mean}" "${clear_p50}" "${clear_p95}" \
    >>"${OUT_DIR}/profile-summary.tsv"

PROFILE_METRICS="${PROFILE_PREFIX}.metrics.csv"
HASH_LOW="$(metric_last "${PROFILE_METRICS}" terrain.backdrop content_hash_low32)"
HASH_HIGH="$(metric_last "${PROFILE_METRICS}" terrain.backdrop content_hash_high32)"
printf -v CONTENT_HASH '0x%08x%08x' "${HASH_HIGH%%.*}" "${HASH_LOW%%.*}"
SOURCE_SAMPLES="$(metric_last "${PROFILE_METRICS}" terrain.backdrop source_samples)"
PRODUCT_TRIANGLES="$(metric_last "${PROFILE_METRICS}" terrain.backdrop product_render_triangles)"
RENDER_STRIDE="$(metric_last "${PROFILE_METRICS}" terrain.backdrop render_stride)"
MATERIAL_BYTES="$(metric_last "${PROFILE_METRICS}" terrain.backdrop material_texture_bytes)"
MANIFEST_PATH="${HEIGHTFIELD}"
if [[ -d "${HEIGHTFIELD}" ]]; then
    MANIFEST_PATH="${HEIGHTFIELD}/heightfield.json"
fi

jq -n \
    --arg schema "cubey.terrain.material-v2-review.v1" \
    --arg lane "${MODE}" \
    --arg git_revision "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    --arg executable "${APP}" \
    --arg heightfield_manifest "${MANIFEST_PATH}" \
    --arg elevation_sha256 "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")" \
    --arg product_content_hash "${CONTENT_HASH}" \
    --argjson product_render_triangles "${PRODUCT_TRIANGLES%%.*}" \
    --argjson source_samples "${SOURCE_SAMPLES%%.*}" \
    --argjson render_stride "${RENDER_STRIDE%%.*}" \
    --argjson material_texture_bytes "${MATERIAL_BYTES%%.*}" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" \
    --argjson capture_count "${#ALL_FILES[@]}" \
    --argjson profile_frames "${FRAMES}" \
    --argjson profile_warmup_frames "${WARMUP_FRAMES}" \
    '{
        schema: $schema,
        lane: $lane,
        git_revision: $git_revision,
        executable: $executable,
        heightfield_manifest: $heightfield_manifest,
        elevation_sha256: $elevation_sha256,
        product_content_hash: $product_content_hash,
        product_render_triangles: $product_render_triangles,
        source_samples: $source_samples,
        render_stride: $render_stride,
        material_texture_bytes: $material_texture_bytes,
        resolution: {width: $width, height: $height},
        profile: {frames: $profile_frames, warmup_frames: $profile_warmup_frames},
        capture_count: $capture_count
    }' >"${OUT_DIR}/review-metadata.json"

if command -v magick >/dev/null 2>&1; then
    montage_group "${OUT_DIR}/lane-contact-sheet.png" 4x5 ALL_FILES ALL_LABELS
fi

{
    printf '# Terrain Material V2 %s Lane\n\n' "${MODE^}"
    printf -- '- Runtime revision: `%s`\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf -- '- Product hash: `%s`\n' "${CONTENT_HASH}"
    printf -- '- Clear mean / p50 / p95: `%s / %s / %s ms`\n\n' \
        "${clear_mean}" "${clear_p50}" "${clear_p95}"
    printf '| Capture | Group | Arguments |\n'
    printf '|---|---|---|\n'
    tail -n +2 "${MANIFEST}" | while IFS=$'\t' read -r file title group args; do
        printf '| [%s](%s) | %s | `%s` |\n' "${title}" "${file}" "${group}" "${args}"
    done
} >"${OUT_DIR}/index.md"

printf 'Terrain Material V2 %s lane written to %s\n' "${MODE}" "${OUT_DIR}"
