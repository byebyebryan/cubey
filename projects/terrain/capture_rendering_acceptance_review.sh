#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
ASSET_ROOT="${ASSET_ROOT:-${ROOT_DIR}/build/dev/assets/terrain/climate-calibration}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/rendering-acceptance-v1}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-120}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"
PROFILE_ONLY="${PROFILE_ONLY:-0}"

REGIMES=(hot-dry hot-wet cool-wet cold-dry cold-wet)
COOL_WET="${ASSET_ROOT}/cool-wet"

if [[ ! -x "${APP}" ]]; then
    printf 'terrain executable not found: %s\n' "${APP}" >&2
    printf 'Build it with: cmake --build --preset dev --target cubey_project_terrain\n' >&2
    exit 1
fi
if [[ ! -f "${ASSET_ROOT}/calibration-index.json" ]]; then
    printf 'terrain climate calibration assets not found: %s\n' "${ASSET_ROOT}" >&2
    printf '%s\n' \
        'Generate them with: cmake --build --preset dev --target cubey_terrain_generate_climate_calibration_assets' \
        >&2
    exit 1
fi

mkdir -p "${OUT_DIR}/captures" "${OUT_DIR}/profiles"
find "${OUT_DIR}/profiles" -mindepth 1 -delete
if [[ "${PROFILE_ONLY}" != "1" ]]; then
    find "${OUT_DIR}/captures" -mindepth 1 -delete
    find "${OUT_DIR}" -mindepth 1 -maxdepth 1 ! -name captures ! -name profiles -delete
fi

CAPTURE_MANIFEST="${OUT_DIR}/capture-manifest.tsv"
PROFILE_SUMMARY="${OUT_DIR}/profile-summary.tsv"
INDEX="${OUT_DIR}/index.md"

if [[ "${PROFILE_ONLY}" != "1" ]]; then
    printf 'file\ttitle\tgroup\tsource\targs\n' >"${CAPTURE_MANIFEST}"
elif [[ ! -f "${CAPTURE_MANIFEST}" ]]; then
    printf 'profile-only review requires an existing capture manifest: %s\n' \
        "${CAPTURE_MANIFEST}" >&2
    exit 1
fi

COMMON_ARGS=(
    --terrain-surface-model climate-transition
    --terrain-placement selected
    --terrain-camera-preset backdrop
    --terrain-render-stride 3
    --terrain-foreground-height 200
    --terrain-backdrop-orbit-radius 100
    --terrain-backdrop-elevation 8
    --terrain-surface-detail filtered-detail
    --terrain-shadows
    --time-of-day-mode manual
    --sun-elevation 38
    --sun-azimuth -42
    --pause-time
    --no-clouds
)

capture_source() {
    local source_dir="$1"
    local name="$2"
    local title="$3"
    local group="$4"
    shift 4
    local output="${OUT_DIR}/captures/${name}.png"

    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${source_dir}" \
        --terrain-surface-fields "${source_dir}" \
        "${COMMON_ARGS[@]}" "$@" --output "${output}"

    local args="$*"
    args="${args//$'\t'/ }"
    printf '%s\t%s\t%s\t%s\t%s\n' \
        "${output#"${OUT_DIR}/"}" "${title}" "${group}" \
        "${source_dir#"${ASSET_ROOT}/"}" "${args}" >>"${CAPTURE_MANIFEST}"
}

FRAMING_FILES=()
FRAMING_LABELS=()
if [[ "${PROFILE_ONLY}" != "1" ]]; then
for foreground_height in 100 200 500; do
    name="framing-${foreground_height}m"
    capture_source "${COOL_WET}" "${name}" "Cool/wet: ${foreground_height} m" framing \
        --terrain-foreground-height "${foreground_height}"
    FRAMING_FILES+=("${OUT_DIR}/captures/${name}.png")
    FRAMING_LABELS+=("Cool/wet: ${foreground_height} m")
done

LIGHTING_FILES=()
LIGHTING_LABELS=()
for sun_elevation in 38 12 2 -6 -18; do
    for lane in off on visibility; do
        name="lighting-${sun_elevation}-${lane}"
        args=(--sun-elevation "${sun_elevation}")
        case "${lane}" in
        off)
            args+=(--no-terrain-shadows)
            ;;
        on)
            args+=(--terrain-shadows)
            ;;
        visibility)
            args+=(--terrain-shadows --debug-view sun-visibility)
            ;;
        esac
        capture_source "${COOL_WET}" "${name}" \
            "Sun ${sun_elevation} deg: ${lane}" lighting "${args[@]}"
        LIGHTING_FILES+=("${OUT_DIR}/captures/${name}.png")
        LIGHTING_LABELS+=("Sun ${sun_elevation} deg: ${lane}")
    done
done

DIAGNOSTIC_FILES=()
DIAGNOSTIC_LABELS=()
for diagnostic in classification-normal projected-edge material-albedo material-normal \
    ambient-light direct-light; do
    name="diagnostic-${diagnostic}"
    capture_source "${COOL_WET}" "${name}" "Diagnostic: ${diagnostic}" diagnostics \
        --debug-view "${diagnostic}"
    DIAGNOSTIC_FILES+=("${OUT_DIR}/captures/${name}.png")
    DIAGNOSTIC_LABELS+=("Diagnostic: ${diagnostic}")
done

CLIMATE_FILES=()
CLIMATE_LABELS=()
for regime in "${REGIMES[@]}"; do
    source_dir="${ASSET_ROOT}/${regime}"
    for manifest in heightfield.json surface-fields.json; do
        if [[ ! -f "${source_dir}/${manifest}" ]]; then
            printf 'terrain climate source is incomplete: %s/%s\n' \
                "${source_dir}" "${manifest}" >&2
            exit 1
        fi
    done
    name="climate-${regime}"
    capture_source "${source_dir}" "${name}" "Climate: ${regime}" climates
    CLIMATE_FILES+=("${OUT_DIR}/captures/${name}.png")
    CLIMATE_LABELS+=("Climate: ${regime}")
done
fi

external_gpu_busy() {
    command -v nvidia-smi >/dev/null 2>&1 || return 1
    if nvidia-smi pmon -c 1 -s u 2>/dev/null | awk '
        $1 ~ /^[0-9]+$/ && $3 ~ /C/ && $4 ~ /^[0-9]+$/ && $4 + 0 >= 10 {
            busy = 1
        }
        END { exit busy ? 0 : 1 }
    '; then
        return 0
    fi
    nvidia-smi dmon -c 1 -s u 2>/dev/null | awk '
        $1 ~ /^[0-9]+$/ && (($2 ~ /^[0-9]+$/ && $2 + 0 >= 10) ||
                            ($3 ~ /^[0-9]+$/ && $3 + 0 >= 10)) {
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
    "${APP}" --headless --capture video --frames "${FRAMES}" --fps "${FPS}" \
        --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${COOL_WET}" \
        --terrain-surface-fields "${COOL_WET}" \
        --terrain-surface-model climate-transition \
        --terrain-placement selected \
        --terrain-camera-preset backdrop \
        --terrain-render-stride 3 \
        --terrain-foreground-height 200 \
        --terrain-backdrop-orbit-radius 100 \
        --terrain-backdrop-elevation 8 \
        --terrain-surface-detail filtered-detail \
        --no-clouds \
        --profile-output "${prefix}" \
        --profile-warmup-frames "${WARMUP_FRAMES}" \
        "$@" --output "${video}"
    rm -f "${video}"
}

profile_lane steady-control --no-terrain-shadows --time-of-day-mode manual \
    --sun-elevation 38 --sun-azimuth -42 --pause-time
profile_lane steady-candidate --terrain-shadows --time-of-day-mode manual \
    --sun-elevation 38 --sun-azimuth -42 --pause-time
profile_lane moving-clock --terrain-shadows --time-of-day-mode solar \
    --time-hours 10 --day-of-year 172 --latitude-degrees 35 \
    --time-speed-hours-per-second 0.5
profile_lane shadow-saturation --terrain-shadows --time-of-day-mode solar \
    --time-hours 10 --day-of-year 172 --latitude-degrees 35 \
    --time-speed-hours-per-second 2

span_stat() {
    local summary="$1"
    local label="$2"
    local column="$3"
    awk -F, -v label="${label}" -v column="${column}" \
        '$1 == "gpu" && $2 == label { printf "%.6f", $column; found = 1; exit }
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

metric_hash() {
    local metrics="$1"
    local prefix="$2"
    local low high
    low="$(metric_last "${metrics}" terrain.backdrop "${prefix}_low32")"
    high="$(metric_last "${metrics}" terrain.backdrop "${prefix}_high32")"
    printf '0x%08x%08x' "${high%%.*}" "${low%%.*}"
}

printf 'lane\tcombined_mean_ms\tcombined_p50_ms\tcombined_p95_ms\tshadow_p50_ms\tshadow_updates\n' \
    >"${PROFILE_SUMMARY}"
for lane in steady-control steady-candidate moving-clock shadow-saturation; do
    summary="${OUT_DIR}/profiles/${lane}.summary.txt"
    metrics="${OUT_DIR}/profiles/${lane}.metrics.csv"
    totals=()
    for column in 4 6 7; do
        atmosphere="$(span_stat "${summary}" "terrain atmosphere" "${column}")"
        shadow="$(span_stat "${summary}" "terrain shadow" "${column}")"
        terrain="$(span_stat "${summary}" "terrain surface" "${column}")"
        post="$(span_stat "${summary}" "terrain post" "${column}")"
        totals+=("$(awk -v a="${atmosphere}" -v s="${shadow}" -v t="${terrain}" \
            -v p="${post}" 'BEGIN { printf "%.6f", a + s + t + p }')")
    done
    shadow_p50="$(span_stat "${summary}" "terrain shadow" 6)"
    updates="$(metric_last "${metrics}" terrain.shadow update_count)"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${lane}" "${totals[0]}" "${totals[1]}" "${totals[2]}" \
        "${shadow_p50}" "${updates}" >>"${PROFILE_SUMMARY}"
done

if ! awk -F '\t' '
    NR == 1 { next }
    $1 == "steady-control" { control_mean = $2; control_p50 = $3 }
    $1 == "steady-candidate" { candidate_mean = $2; candidate_p50 = $3 }
    $1 == "moving-clock" { moving_mean = $2; moving_p50 = $3 }
    END {
        pass = candidate_mean <= 1.10 && candidate_p50 <= 1.10 &&
               moving_mean <= 1.10 && moving_p50 <= 1.10 &&
               candidate_mean - control_mean <= 0.15 &&
               candidate_p50 - control_p50 <= 0.15
        exit pass ? 0 : 1
    }
' "${PROFILE_SUMMARY}"; then
    printf 'terrain rendering acceptance performance gate failed\n' >&2
    cat "${PROFILE_SUMMARY}" >&2
    exit 1
fi

montage_group() {
    local output="$1"
    local tile="$2"
    local geometry="$3"
    local files_name="$4"
    local labels_name="$5"
    declare -n files_ref="${files_name}"
    declare -n labels_ref="${labels_name}"
    local inputs=()
    for index in "${!files_ref[@]}"; do
        inputs+=(-label "${labels_ref[${index}]}" "${files_ref[${index}]}")
    done
    magick montage "${inputs[@]}" -geometry "${geometry}" -tile "${tile}" "${output}"
}

if [[ "${PROFILE_ONLY}" != "1" ]] && command -v magick >/dev/null 2>&1; then
    montage_group "${OUT_DIR}/framing-contact-sheet.png" 3x1 480x270+8+26 \
        FRAMING_FILES FRAMING_LABELS
    montage_group "${OUT_DIR}/lighting-contact-sheet.png" 3x5 400x225+8+26 \
        LIGHTING_FILES LIGHTING_LABELS
    montage_group "${OUT_DIR}/diagnostics-contact-sheet.png" 3x2 480x270+8+26 \
        DIAGNOSTIC_FILES DIAGNOSTIC_LABELS
    montage_group "${OUT_DIR}/climates-contact-sheet.png" 5x1 320x180+8+26 \
        CLIMATE_FILES CLIMATE_LABELS
fi

PROFILE_METRICS="${OUT_DIR}/profiles/steady-candidate.metrics.csv"
MANIFEST_PATH="${COOL_WET}/heightfield.json"
jq -n \
    --arg schema "cubey.terrain.rendering-acceptance.v1" \
    --arg git_revision "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    --arg elevation_sha256 "$(jq -r '.files.elevation.sha256' "${MANIFEST_PATH}")" \
    --arg geometry_hash "$(metric_hash "${PROFILE_METRICS}" geometry_hash)" \
    --arg content_hash "$(metric_hash "${PROFILE_METRICS}" content_hash)" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" \
    --argjson total_triangles "$(metric_last "${PROFILE_METRICS}" terrain.backdrop product_render_triangles)" \
    --argjson center_triangles "$(metric_last "${PROFILE_METRICS}" terrain.backdrop center_render_triangles)" \
    --argjson source_samples "$(metric_last "${PROFILE_METRICS}" terrain.backdrop source_samples)" \
    --argjson material_bytes "$(metric_last "${PROFILE_METRICS}" terrain.backdrop material_texture_bytes)" \
    --argjson shadow_extent "$(metric_last "${PROFILE_METRICS}" terrain.shadow map_extent)" \
    --argjson shadow_texel_world_m "$(metric_last "${PROFILE_METRICS}" terrain.shadow texel_world_m)" \
    --argjson shadow_depth_span_m "$(metric_last "${PROFILE_METRICS}" terrain.shadow depth_span_m)" \
    --argjson frames "${FRAMES}" \
    --argjson warmup_frames "${WARMUP_FRAMES}" \
    '{
        schema: $schema,
        git_revision: $git_revision,
        source: {regime: "cool-wet", elevation_sha256: $elevation_sha256},
        product: {
            geometry_hash: $geometry_hash,
            content_hash: $content_hash,
            total_render_triangles: $total_triangles,
            center_render_triangles: $center_triangles,
            source_samples: $source_samples,
            material_texture_bytes: $material_bytes
        },
        shadow: {
            extent: $shadow_extent,
            texel_world_m: $shadow_texel_world_m,
            depth_span_m: $shadow_depth_span_m
        },
        capture: {
            width: $width,
            height: $height,
            foreground_heights_m: [100, 200, 500],
            sun_elevations_degrees: [38, 12, 2, -6, -18]
        },
        profile: {frames: $frames, warmup_frames: $warmup_frames}
    }' >"${OUT_DIR}/review-metadata.json"

{
    printf '# Terrain Rendering Acceptance V1 Review\n\n'
    printf 'Start with `framing-contact-sheet.png` for the 100/200/500 m envelope. '
    printf '`lighting-contact-sheet.png` separates shadow sampling from the final surface, '
    printf '`diagnostics-contact-sheet.png` isolates topology, material, and lighting, and '
    printf '`climates-contact-sheet.png` checks the result across all generated regimes.\n\n'
    printf -- '- Runtime revision: `%s`\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Primary source: cool/wet selected placement\n'
    printf -- '- Default foreground height: 200 m\n\n'
    printf 'The `shadow-saturation` lane drives a two-hour-per-second clock so the map '
    printf 'refreshes every frame. It records the cache-saturation boundary and is not a '
    printf 'product timing gate; `moving-clock` uses the default 0.5-hour-per-second cadence.\n\n'
    printf '```tsv\n'
    cat "${PROFILE_SUMMARY}"
    printf '```\n\n'
    printf '| Capture | Group | Source | Arguments |\n'
    printf '|---|---|---|---|\n'
    tail -n +2 "${CAPTURE_MANIFEST}" | while IFS=$'\t' read -r file title group source args; do
        printf '| [%s](%s) | %s | %s | `%s` |\n' \
            "${title}" "${file}" "${group}" "${source}" "${args}"
    done
} >"${INDEX}"

printf 'terrain rendering acceptance review: wrote %s\n' "${OUT_DIR}"
