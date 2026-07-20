#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
HEIGHTFIELD="${HEIGHTFIELD:-${ROOT_DIR}/build/dev/assets/terrain/default}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/rendering-envelope-v1}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-150}"
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
CAPTURE_FILES=()
CAPTURE_LABELS=()
MACRO_FILES=()
MACRO_LABELS=()
QUALIFIED_FILES=()
QUALIFIED_LABELS=()
STRESS_FILES=()
STRESS_LABELS=()
STRIDE_SURFACE_FILES=()
STRIDE_SURFACE_LABELS=()
STRIDE_CLAY_FILES=()
STRIDE_CLAY_LABELS=()
STRIDE_EDGE_FILES=()
STRIDE_EDGE_LABELS=()

COMMON_ARGS=(
    --terrain-heightfield "${HEIGHTFIELD}"
    --terrain-placement selected
    --terrain-camera-preset backdrop
    --terrain-surface-detail filtered-detail
    --terrain-shadows
    --time-of-day-mode solar
    --time-hours 9
    --day-of-year 172
    --latitude-degrees 35
    --pause-time
)

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
        "${COMMON_ARGS[@]}" \
        "$@" \
        --output "${OUT_DIR}/${name}.png"

    local args="$*"
    args="${args//$'\t'/ }"
    CAPTURE_FILES+=("${OUT_DIR}/${name}.png")
    CAPTURE_LABELS+=("${title}")
    printf '%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${group}" "${args}" \
        >>"${MANIFEST}"

    case "${group}" in
    macro)
        MACRO_FILES+=("${OUT_DIR}/${name}.png")
        MACRO_LABELS+=("${title}")
        ;;
    qualified)
        QUALIFIED_FILES+=("${OUT_DIR}/${name}.png")
        QUALIFIED_LABELS+=("${title}")
        ;;
    stress)
        STRESS_FILES+=("${OUT_DIR}/${name}.png")
        STRESS_LABELS+=("${title}")
        ;;
    stride-surface)
        STRIDE_SURFACE_FILES+=("${OUT_DIR}/${name}.png")
        STRIDE_SURFACE_LABELS+=("${title}")
        ;;
    stride-clay)
        STRIDE_CLAY_FILES+=("${OUT_DIR}/${name}.png")
        STRIDE_CLAY_LABELS+=("${title}")
        ;;
    stride-edge)
        STRIDE_EDGE_FILES+=("${OUT_DIR}/${name}.png")
        STRIDE_EDGE_LABELS+=("${title}")
        ;;
    esac
}

for heading in 0 90 180 270; do
    capture "macro-clear-${heading}" "Clear, 500 m, ${heading} deg" macro \
        --terrain-render-stride 3 --terrain-foreground-height 500 \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth "${heading}" --no-clouds
done
for heading in 0 90 180 270; do
    capture "macro-cloud-${heading}" "Fair cloud, 500 m, ${heading} deg" macro \
        --terrain-render-stride 3 --terrain-foreground-height 500 \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth "${heading}" --clouds \
        --cloud-weather-preset fair-weather
done
for heading in 0 90 180 270; do
    capture "macro-low-${heading}" "Clear stress, 100 m, ${heading} deg" macro \
        --terrain-render-stride 3 --terrain-foreground-height 100 \
        --terrain-backdrop-orbit-radius 100 --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth "${heading}" --no-clouds
done

for heading in 90 180; do
    for radius in 50 250; do
        for elevation in 0 30; do
            capture "qualified-h${heading}-r${radius}-e${elevation}" \
                "Qualified: h${heading}, r${radius}, e${elevation}" qualified \
                --terrain-render-stride 3 --terrain-foreground-height 500 \
                --terrain-backdrop-orbit-radius "${radius}" \
                --terrain-backdrop-elevation "${elevation}" \
                --terrain-backdrop-azimuth "${heading}" --no-clouds
        done
    done
done

for heading in 90 180; do
    for radius in 500 1000; do
        for elevation in 0 30; do
            capture "stress-h${heading}-r${radius}-e${elevation}" \
                "Stress: h${heading}, r${radius}, e${elevation}" stress \
                --terrain-render-stride 3 --terrain-foreground-height 100 \
                --terrain-backdrop-orbit-radius "${radius}" \
                --terrain-backdrop-elevation "${elevation}" \
                --terrain-backdrop-azimuth "${heading}" --no-clouds
        done
    done
done

STRIDE_VIEW_IDS=(qualified-90 qualified-180 stress-90)
STRIDE_VIEW_LABELS=("Qualified h90" "Qualified h180" "Stress h90")
STRIDE_VIEW_ARGS=(
    "--terrain-foreground-height 500 --terrain-backdrop-orbit-radius 250 --terrain-backdrop-elevation 0 --terrain-backdrop-azimuth 90"
    "--terrain-foreground-height 500 --terrain-backdrop-orbit-radius 250 --terrain-backdrop-elevation 0 --terrain-backdrop-azimuth 180"
    "--terrain-foreground-height 100 --terrain-backdrop-orbit-radius 1000 --terrain-backdrop-elevation 0 --terrain-backdrop-azimuth 90"
)

for index in "${!STRIDE_VIEW_IDS[@]}"; do
    read -r -a view_args <<<"${STRIDE_VIEW_ARGS[${index}]}"
    for stride in 3 1; do
        capture "stride-surface-${STRIDE_VIEW_IDS[${index}]}-s${stride}" \
            "${STRIDE_VIEW_LABELS[${index}]}, stride ${stride}" stride-surface \
            --terrain-render-stride "${stride}" "${view_args[@]}" --no-clouds
        capture "stride-clay-${STRIDE_VIEW_IDS[${index}]}-s${stride}" \
            "${STRIDE_VIEW_LABELS[${index}]}, stride ${stride}" stride-clay \
            --terrain-render-stride "${stride}" "${view_args[@]}" --no-clouds \
            --debug-view clay
        capture "stride-edge-${STRIDE_VIEW_IDS[${index}]}-s${stride}" \
            "${STRIDE_VIEW_LABELS[${index}]}, stride ${stride}" stride-edge \
            --terrain-render-stride "${stride}" "${view_args[@]}" --no-clouds \
            --debug-view projected-edge
    done
done

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
            if ((idle_samples >= 2)); then
                return 0
            fi
        fi
        sleep 2
    done
    return 1
}

profile_lane() {
    local lane="$1"
    shift
    local prefix="${OUT_DIR}/profiles/${lane}"
    local video="${OUT_DIR}/profiles/${lane}.mp4"

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
        "${COMMON_ARGS[@]}" \
        --terrain-foreground-height 500 \
        --terrain-backdrop-orbit-radius 100 \
        --terrain-backdrop-elevation 8 \
        --terrain-backdrop-azimuth 90 \
        --profile-output "${prefix}" \
        --profile-warmup-frames "${WARMUP_FRAMES}" \
        "$@" \
        --output "${video}"
    rm -f "${video}"
}

profile_lane clear-stride3 --terrain-render-stride 3 --no-clouds
profile_lane clear-stride1 --terrain-render-stride 1 --no-clouds
profile_lane cloud-stride3 --terrain-render-stride 3 --clouds \
    --cloud-weather-preset fair-weather

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
    local scope="$2"
    local output="$3"
    awk -F, -v scope="${scope}" '
        NR > 1 && $2 == "gpu" {
            terrain = $3 == "terrain shadow" || $3 == "terrain surface" ||
                      $3 == "terrain stage proxy"
            clear = terrain || $3 == "terrain atmosphere" || $3 == "terrain post"
            cloud = $3 == "cloud march" || $3 == "cloud temporal" ||
                    $3 == "cloud composite"
            include = (scope == "terrain" && terrain) || (scope == "clear" && clear) ||
                      (scope == "cloud" && cloud) || (scope == "full" && (clear || cloud))
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

printf 'lane\trender_stride\tproduct_triangles\tterrain_mean_ms\tterrain_p50_ms\tterrain_p95_ms\tclear_mean_ms\tclear_p50_ms\tclear_p95_ms\tcloud_mean_ms\tcloud_p50_ms\tcloud_p95_ms\tfull_mean_ms\tfull_p50_ms\tfull_p95_ms\n' \
    >"${PROFILE_SUMMARY}"
for lane in clear-stride3 clear-stride1 cloud-stride3; do
    prefix="${OUT_DIR}/profiles/${lane}"
    for scope in terrain clear cloud full; do
        aggregate_gpu_values "${prefix}.passes.csv" "${scope}" \
            "${prefix}.${scope}-values.tmp"
    done
    IFS=$'\t' read -r terrain_mean terrain_p50 terrain_p95 \
        < <(value_stats "${prefix}.terrain-values.tmp")
    IFS=$'\t' read -r clear_mean clear_p50 clear_p95 \
        < <(value_stats "${prefix}.clear-values.tmp")
    IFS=$'\t' read -r cloud_mean cloud_p50 cloud_p95 \
        < <(value_stats "${prefix}.cloud-values.tmp")
    IFS=$'\t' read -r full_mean full_p50 full_p95 \
        < <(value_stats "${prefix}.full-values.tmp")
    stride="$(metric_last "${prefix}.metrics.csv" terrain.backdrop render_stride)"
    triangles="$(metric_last "${prefix}.metrics.csv" terrain.backdrop product_render_triangles)"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${lane}" "${stride%%.*}" "${triangles%%.*}" \
        "${terrain_mean}" "${terrain_p50}" "${terrain_p95}" \
        "${clear_mean}" "${clear_p50}" "${clear_p95}" \
        "${cloud_mean}" "${cloud_p50}" "${cloud_p95}" \
        "${full_mean}" "${full_p50}" "${full_p95}" >>"${PROFILE_SUMMARY}"
    rm -f "${prefix}."*-values.tmp
done

read -r clear_stride3_mean clear_stride3_p50 < <(
    awk -F'\t' '$1 == "clear-stride3" { print $7, $8 }' "${PROFILE_SUMMARY}"
)
if ! awk -v mean="${clear_stride3_mean}" -v p50="${clear_stride3_p50}" \
    'BEGIN { exit mean <= 1.0 && p50 <= 1.0 ? 0 : 1 }'; then
    printf 'clear stride-3 budget failed: mean=%s ms, p50=%s ms\n' \
        "${clear_stride3_mean}" "${clear_stride3_p50}" >&2
    exit 1
fi

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

if command -v magick >/dev/null 2>&1; then
    montage_group "${OUT_DIR}/macro-contact-sheet.png" 4x3 MACRO_FILES MACRO_LABELS
    montage_group "${OUT_DIR}/qualified-envelope-contact-sheet.png" 4x2 \
        QUALIFIED_FILES QUALIFIED_LABELS
    montage_group "${OUT_DIR}/stress-envelope-contact-sheet.png" 4x2 \
        STRESS_FILES STRESS_LABELS
    montage_group "${OUT_DIR}/stride-surface-contact-sheet.png" 2x3 \
        STRIDE_SURFACE_FILES STRIDE_SURFACE_LABELS
    montage_group "${OUT_DIR}/stride-clay-contact-sheet.png" 2x3 \
        STRIDE_CLAY_FILES STRIDE_CLAY_LABELS
    montage_group "${OUT_DIR}/stride-edge-contact-sheet.png" 2x3 \
        STRIDE_EDGE_FILES STRIDE_EDGE_LABELS
fi

MANIFEST_PATH="${HEIGHTFIELD}"
if [[ -d "${HEIGHTFIELD}" ]]; then
    MANIFEST_PATH="${HEIGHTFIELD}/heightfield.json"
fi
GIT_REVISION="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
PROFILE_METRICS="${OUT_DIR}/profiles/clear-stride3.metrics.csv"
HASH_LOW="$(metric_last "${PROFILE_METRICS}" terrain.backdrop content_hash_low32)"
HASH_HIGH="$(metric_last "${PROFILE_METRICS}" terrain.backdrop content_hash_high32)"
printf -v CONTENT_HASH '0x%08x%08x' "${HASH_HIGH%%.*}" "${HASH_LOW%%.*}"
SOURCE_SAMPLES="$(metric_last "${PROFILE_METRICS}" terrain.backdrop source_samples)"
STRIDE3_TRIANGLES="$(metric_last "${PROFILE_METRICS}" terrain.backdrop product_render_triangles)"
STRIDE1_TRIANGLES="$(metric_last "${OUT_DIR}/profiles/clear-stride1.metrics.csv" \
    terrain.backdrop product_render_triangles)"

jq -n \
    --arg schema "cubey.terrain.rendering-envelope-review.v1" \
    --arg git_revision "${GIT_REVISION}" \
    --arg executable "${APP}" \
    --arg heightfield_manifest "${MANIFEST_PATH}" \
    --arg elevation_sha256 "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")" \
    --arg stride3_content_hash "${CONTENT_HASH}" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" \
    --argjson capture_count "${#CAPTURE_FILES[@]}" \
    --argjson source_samples "${SOURCE_SAMPLES%%.*}" \
    --argjson stride3_triangles "${STRIDE3_TRIANGLES%%.*}" \
    --argjson stride1_triangles "${STRIDE1_TRIANGLES%%.*}" \
    --argjson profile_frames "${FRAMES}" \
    --argjson profile_warmup_frames "${WARMUP_FRAMES}" \
    '{
        schema: $schema,
        git_revision: $git_revision,
        executable: $executable,
        heightfield_manifest: $heightfield_manifest,
        elevation_sha256: $elevation_sha256,
        stride3_content_hash: $stride3_content_hash,
        source_samples: $source_samples,
        geometry: {
            stride3_triangles: $stride3_triangles,
            stride1_triangles: $stride1_triangles
        },
        resolution: {width: $width, height: $height},
        environment: {
            time_mode: "solar",
            time_hours: 9,
            day_of_year: 172,
            latitude_degrees: 35,
            paused: true
        },
        camera: {
            qualified_foreground_height_m: 500,
            qualified_orbit_radius_m: [50, 250],
            stress_foreground_height_m: 100,
            stress_orbit_radius_m: [500, 1000]
        },
        profile: {frames: $profile_frames, warmup_frames: $profile_warmup_frames},
        capture_count: $capture_count
    }' >"${OUT_DIR}/review-metadata.json"

{
    printf '# Terrain Rendering Envelope V1 Review\n\n'
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Runtime revision: `%s`\n' "${GIT_REVISION}"
    printf -- '- Heightfield SHA-256: `%s`\n' \
        "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")"
    printf -- '- Stride 3 product hash: `%s`\n' "${CONTENT_HASH}"
    printf -- '- Cached source samples: %s\n' "${SOURCE_SAMPLES%%.*}"
    printf -- '- Render triangles: stride 3 = %s; stride 1 = %s\n\n' \
        "${STRIDE3_TRIANGLES%%.*}" "${STRIDE1_TRIANGLES%%.*}"
    printf 'Review `macro-contact-sheet.png` first, then the two envelope sheets. '
    printf 'The stride surface and clay sheets decide whether topology is visibly limiting; '
    printf 'the projected-edge sheet is supporting evidence.\n\n'
    printf '```tsv\n'
    cat "${PROFILE_SUMMARY}"
    printf '```\n\n'
    printf '| Capture | Group | Arguments |\n'
    printf '|---|---|---|\n'
    tail -n +2 "${MANIFEST}" | while IFS=$'\t' read -r file title group args; do
        printf '| [%s](%s) | %s | `%s` |\n' "${title}" "${file}" "${group}" "${args}"
    done
} >"${INDEX}"

printf 'Terrain rendering-envelope review written to %s\n' "${OUT_DIR}"
