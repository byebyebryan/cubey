#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/cache/terrain/sources/v1/default}"
MODE="${1:-}"
OUT_ROOT="${2:-${ROOT_DIR}/outputs/terrain/visual-closure-v1}"
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

comparison_sheet() {
    local output="$1"
    local tile="$2"
    shift 2
    local files=()
    local labels=()
    local name
    for name in "$@"; do
        files+=("${OUT_ROOT}/control/${name}.png"
                "${OUT_ROOT}/candidate/${name}.png")
        labels+=("Control: ${name}" "Candidate: ${name}")
    done
    montage_group "${output}" "${tile}" files labels
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
        awk -F'\t' '$1 == "steady" {print $2, $3, $4}' \
            "${CONTROL_DIR}/profile-summary.tsv"
    )
    read -r candidate_mean candidate_p50 candidate_p95 < <(
        awk -F'\t' '$1 == "steady" {print $2, $3, $4}' \
            "${CANDIDATE_DIR}/profile-summary.tsv"
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

    printf 'file\tae_pixels\tnormalized_rmse\n' >"${OUT_ROOT}/image-comparison.tsv"
    while IFS=$'\t' read -r file _title _group _args; do
        [[ "${file}" == "file" ]] && continue
        name="${file%.png}"
        control_image="${CONTROL_DIR}/${file}"
        candidate_image="${CANDIDATE_DIR}/${file}"
        ae_output="$(magick compare -metric AE "${control_image}" "${candidate_image}" \
            null: 2>&1 || true)"
        ae="${ae_output%% *}"
        rmse_output="$(magick compare -metric RMSE "${control_image}" "${candidate_image}" \
            null: 2>&1 || true)"
        rmse="$(sed -n 's/.*(\([^)]*\)).*/\1/p' <<<"${rmse_output}")"
        printf '%s\t%s\t%s\n' "${file}" "${ae}" "${rmse:-0}" \
            >>"${OUT_ROOT}/image-comparison.tsv"
    done <"${CONTROL_DIR}/manifest.tsv"

    flat_ae="$(awk -F'\t' '$1 == "flat-control.png" {print $2}' \
        "${OUT_ROOT}/image-comparison.tsv")"
    if [[ "${flat_ae}" != "0" ]]; then
        printf 'flat control changed by %s pixels\n' "${flat_ae}" >&2
        exit 1
    fi

    comparison_sheet "${OUT_ROOT}/qualified-comparison.png" 2x4 \
        qualified-0 qualified-90 qualified-180 qualified-270
    comparison_sheet "${OUT_ROOT}/solar-comparison.png" 4x4 \
        selected-raking selected-twilight selected-night selected-deep-night \
        raw-day raw-raking raw-twilight raw-night
    comparison_sheet "${OUT_ROOT}/distance-comparison.png" 4x2 \
        far-selected far-raw-center stress-selected
    comparison_sheet "${OUT_ROOT}/cloud-comparison.png" 4x1 \
        cloud-day cloud-twilight
    comparison_sheet "${OUT_ROOT}/diagnostic-comparison.png" 4x3 \
        diagnostic-albedo diagnostic-normal diagnostic-roughness \
        diagnostic-ambient diagnostic-direct

    {
        printf '# Terrain V1 Visual Closure Review\n\n'
        printf -- '- Frozen metadata: `%s`\n' "${control_frozen}"
        printf -- '- Control revision: `%s`\n' \
            "$(jq -r '.git_revision' "${CONTROL_DIR}/review-metadata.json")"
        printf -- '- Candidate revision: `%s`\n' \
            "$(jq -r '.git_revision' "${CANDIDATE_DIR}/review-metadata.json")"
        printf -- '- Flat-control changed pixels: `%s`\n\n' "${flat_ae}"
        printf '| Lane | Steady mean | Steady p50 | Steady p95 |\n'
        printf '|---|---:|---:|---:|\n'
        printf '| Control | %.6f ms | %.6f ms | %.6f ms |\n' \
            "${control_mean}" "${control_p50}" "${control_p95}"
        printf '| Candidate | %.6f ms | %.6f ms | %.6f ms |\n\n' \
            "${candidate_mean}" "${candidate_p50}" "${candidate_p95}"
        printf 'Review `qualified-comparison.png` first, followed by '
        printf '`solar-comparison.png`, `distance-comparison.png`, and '
        printf '`diagnostic-comparison.png`. The stress view reports the V1 limit; '
        printf 'it does not expand the far-backdrop contract.\n'
    } >"${OUT_ROOT}/index.md"

    printf 'Terrain V1 visual closure comparison finalized at %s\n' "${OUT_ROOT}"
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
    --terrain-surface-model mineral-control
    --terrain-camera-preset backdrop
    --terrain-render-stride 3
    --terrain-surface-detail filtered-detail
    --terrain-shadows
    --pause-time
)
DAY_ARGS=(--time-of-day-mode manual --sun-elevation 38 --sun-azimuth 42)
RAKING_ARGS=(--time-of-day-mode manual --sun-elevation 12 --sun-azimuth 42)
TWILIGHT_ARGS=(--time-of-day-mode manual --sun-elevation 2 --sun-azimuth 42)
NIGHT_ARGS=(--time-of-day-mode manual --sun-elevation -6 --sun-azimuth 42)
DEEP_NIGHT_ARGS=(--time-of-day-mode manual --sun-elevation -18 --sun-azimuth 42)

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
    capture "qualified-${heading}" "Selected day ${heading} deg" qualified \
        --terrain-placement selected --terrain-foreground-height 200 \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth "${heading}" "${DAY_ARGS[@]}" --no-clouds
done

capture selected-raking "Selected raking" solar \
    --terrain-placement selected --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${RAKING_ARGS[@]}" --no-clouds
capture selected-twilight "Selected twilight" solar \
    --terrain-placement selected --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${TWILIGHT_ARGS[@]}" --no-clouds
capture selected-night "Selected night" solar \
    --terrain-placement selected --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${NIGHT_ARGS[@]}" --no-clouds
capture selected-deep-night "Selected deep night" solar \
    --terrain-placement selected --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${DEEP_NIGHT_ARGS[@]}" --no-clouds

capture raw-day "Raw center day" placement \
    --terrain-placement raw-center --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${DAY_ARGS[@]}" --no-clouds
capture raw-raking "Raw center raking" placement \
    --terrain-placement raw-center --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${RAKING_ARGS[@]}" --no-clouds
capture raw-twilight "Raw center twilight" placement \
    --terrain-placement raw-center --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${TWILIGHT_ARGS[@]}" --no-clouds
capture raw-night "Raw center night" placement \
    --terrain-placement raw-center --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${NIGHT_ARGS[@]}" --no-clouds

capture far-selected "Selected 500 m" distance \
    --terrain-placement selected --terrain-foreground-height 500 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${DAY_ARGS[@]}" --no-clouds
capture far-raw-center "Raw center 500 m" distance \
    --terrain-placement raw-center --terrain-foreground-height 500 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${DAY_ARGS[@]}" --no-clouds
capture stress-selected "Selected 100 m stress" stress \
    --terrain-placement selected --terrain-foreground-height 100 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${DAY_ARGS[@]}" --no-clouds

capture cloud-day "Selected fair clouds day" cloud \
    --terrain-placement selected --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${DAY_ARGS[@]}" \
    --clouds --cloud-weather-preset fair-weather
capture cloud-twilight "Selected fair clouds twilight" cloud \
    --terrain-placement selected --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${TWILIGHT_ARGS[@]}" \
    --clouds --cloud-weather-preset fair-weather

for diagnostic in albedo normal roughness ambient direct; do
    case "${diagnostic}" in
    albedo) debug_view=material-albedo ;;
    normal) debug_view=material-normal ;;
    roughness) debug_view=material-roughness ;;
    ambient) debug_view=ambient-light ;;
    direct) debug_view=direct-light ;;
    esac
    capture "diagnostic-${diagnostic}" "${diagnostic} diagnostic" diagnostic \
        --terrain-placement selected --terrain-foreground-height 200 \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth 90 "${DAY_ARGS[@]}" \
        --debug-view "${debug_view}" --no-clouds
done

capture flat-control "Flat invariant" invariant \
    --terrain-placement selected --terrain-foreground-height 200 \
    --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
    --terrain-backdrop-azimuth 90 "${DAY_ARGS[@]}" \
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
        if ((idle_samples >= 2)); then
            break
        fi
    fi
    sleep 2
done
if ((idle_samples < 2)); then
    printf 'GPU remained busy before profile lane %s\n' "${MODE}" >&2
    exit 1
fi

printf 'profile\tmean_ms\tp50_ms\tp95_ms\n' >"${OUT_DIR}/profile-summary.tsv"
profile_lane() {
    local name="$1"
    shift
    local prefix="${PROFILE_DIR}/${name}"
    local video="${PROFILE_DIR}/${name}.mp4"
    local values="${PROFILE_DIR}/${name}-values.tmp"

    "${APP}" --headless --capture video --frames "${FRAMES}" --fps "${FPS}" \
        --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${HEIGHTFIELD}" \
        --terrain-surface-model mineral-control \
        --terrain-camera-preset backdrop --terrain-render-stride 3 \
        --terrain-surface-detail filtered-detail --terrain-shadows \
        --terrain-placement selected --terrain-foreground-height 200 \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth 90 --no-clouds "$@" \
        --profile-output "${prefix}" --profile-warmup-frames "${WARMUP_FRAMES}" \
        --output "${video}"
    rm -f "${video}"
    aggregate_gpu_values "${prefix}.passes.csv" "${values}"
    IFS=$'\t' read -r mean p50 p95 < <(value_stats "${values}")
    rm -f "${values}"
    printf '%s\t%s\t%s\t%s\n' "${name}" "${mean}" "${p50}" "${p95}" \
        >>"${OUT_DIR}/profile-summary.tsv"
}

profile_lane steady "${DAY_ARGS[@]}" --pause-time
profile_lane moving \
    --time-of-day-mode solar --time-hours 9 --day-of-year 172 \
    --latitude-degrees 35

PROFILE_METRICS="${PROFILE_DIR}/steady.metrics.csv"
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
    --arg schema "cubey.terrain.visual-closure-review.v1" \
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
    montage_group "${OUT_DIR}/lane-contact-sheet.png" 4x6 ALL_FILES ALL_LABELS
fi

{
    printf '# Terrain V1 Visual Closure %s Lane\n\n' "${MODE^}"
    printf -- '- Runtime revision: `%s`\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf -- '- Product hash: `%s`\n\n' "${CONTENT_HASH}"
    printf '| Profile | Mean | P50 | P95 |\n'
    printf '|---|---:|---:|---:|\n'
    tail -n +2 "${OUT_DIR}/profile-summary.tsv" |
        while IFS=$'\t' read -r profile mean p50 p95; do
            printf '| %s | %.6f ms | %.6f ms | %.6f ms |\n' \
                "${profile}" "${mean}" "${p50}" "${p95}"
        done
    printf '\n| Capture | Group | Arguments |\n'
    printf '|---|---|---|\n'
    tail -n +2 "${MANIFEST}" |
        while IFS=$'\t' read -r file title group args; do
            printf '| [%s](%s) | %s | `%s` |\n' \
                "${title}" "${file}" "${group}" "${args}"
        done
} >"${OUT_DIR}/index.md"

printf 'Terrain V1 visual closure %s lane written to %s\n' "${MODE}" "${OUT_DIR}"
